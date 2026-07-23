package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"io"
	"log"
	"math"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"unsafe"

	"github.com/google/uuid"
)

func isMenuLevel(level string) bool {
	return level == "" || level == "tdmainmenu" || level == "entry"
}

// levelsMatch treats synthetic "gameplay" (Set Gameplay placeholder) as
// compatible with any concrete non-menu map so UDP/interact keep working
// while clients upgrade to e.g. tutorial_p.
func levelsMatch(a, b string) bool {
	if a == b {
		return true
	}
	if a == "gameplay" && !isMenuLevel(b) && b != "gameplay" {
		return true
	}
	if b == "gameplay" && !isMenuLevel(a) && a != "gameplay" {
		return true
	}
	return false
}

func init() {
	logPath := filepath.Join(os.TempDir(), "mmultiplayer-server.log")
	f, err := os.OpenFile(logPath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err == nil {
		log.SetOutput(io.MultiWriter(os.Stdout, f))
	} else {
		log.SetOutput(os.Stdout)
	}
	log.SetFlags(log.LstdFlags | log.Lmicroseconds)
	log.Println("server log initialized")
}

const (
	DefaultPort          = "5222"
	DefaultDiscoveryPort = "5223"
	PacketSizeLegacy     = 676
	PacketSizeVelocity   = 688 // phase4: +12 Velocity
	PacketSize           = 690 // B3-lite: +2 MovementState/Physics
	TagGameMode          = "tag"
	TagTouchMeters       = 1.3
	InteractMaxMeters    = 3.0
	CharacterFaith       = iota
	CharacterKate
	CharacterCeleste
	CharacterAssaultCeleste
	CharacterJacknife
	CharacterMiller
	CharacterKreeg
	CharacterPersuitCop
	CharacterGhost
	CharacterMax
)

const (
	HeartbeatInterval = 2 * time.Second
	ClientTimeout     = 45 * time.Second
	TcpWriteTimeout   = 2 * time.Second
)

type Packet struct {
	Id uint32
}

type position struct {
	x float64
	y float64
	z float64
}

func distanceMeters(from, to position) float64 {
	dx := to.x - from.x
	dy := to.y - from.y
	dz := to.z - from.z
	return math.Sqrt(dx*dx+dy*dy+dz*dz) / 100.0
}

func positionFromPacket(buf []byte) (position, bool) {
	if len(buf) < 16 {
		return position{}, false
	}
	return position{
		x: float64(math.Float32frombits(binary.LittleEndian.Uint32(buf[4:8]))),
		y: float64(math.Float32frombits(binary.LittleEndian.Uint32(buf[8:12]))),
		z: float64(math.Float32frombits(binary.LittleEndian.Uint32(buf[12:16]))),
	}, true
}

type Client struct {
	sync.RWMutex
	writeMu    sync.Mutex
	Tcp        net.Conn
	Id         uint32
	Room       *Room
	Name       string
	Character  uint32
	Level      string
	LastPacket []byte
	UdpAddr    net.Addr // set on each UDP pose packet (needed for push-relay)
	Position   position
	HasPos     bool
	LastSeen   time.Time
}

func (client *Client) SendMessage(msg interface{}) {
	r, err := json.Marshal(msg)
	if err != nil {
		return
	}

	payload := append(r, 0)
	client.writeMu.Lock()
	defer client.writeMu.Unlock()
	if client.Tcp != nil {
		_ = client.Tcp.SetWriteDeadline(time.Now().Add(TcpWriteTimeout))
		if _, err := client.Tcp.Write(payload); err != nil {
			log.Printf("tcp write failed client=%q id=%x err=%v\n", client.Name, client.Id, err)
		}
		_ = client.Tcp.SetWriteDeadline(time.Time{})
	}
}

func (client *Client) CloseTcp() {
	client.writeMu.Lock()
	defer client.writeMu.Unlock()
	if client.Tcp != nil {
		_ = client.Tcp.Close()
		client.Tcp = nil
	}
}

func (client *Client) Touch() {
	client.Lock()
	client.LastSeen = time.Now()
	client.Unlock()
}

func (client *Client) LastSeenAge() time.Duration {
	client.RLock()
	lastSeen := client.LastSeen
	client.RUnlock()
	return time.Since(lastSeen)
}

func removeClientFromRoom(client *Client) {
	if client == nil || client.Room == nil {
		return
	}

	room := client.Room
	var newClients []*Client
	removed := false
	for _, c := range room.Clients {
		if c.Id != client.Id {
			newClients = append(newClients, c)
		} else {
			removed = true
		}
	}

	room.Clients = newClients
	system.RemoveClient(client.Id)
	if !removed {
		return
	}

	if len(newClients) > 0 {
		go room.SendMessageExcept(client.Id, map[string]interface{}{
			"type": "disconnect",
			"id":   client.Id,
		})
	}

	room.onPlayerRemoved(client)
}

func getPort() string {
	if port := strings.TrimSpace(os.Getenv("PORT")); port != "" {
		return port
	}
	return DefaultPort
}

func getDiscoveryPort() string {
	if port := strings.TrimSpace(os.Getenv("DISCOVERY_PORT")); port != "" {
		return port
	}
	return DefaultDiscoveryPort
}

type Room struct {
	Name            string
	Clients         []*Client
	mu              sync.Mutex
	gameMode        string
	taggedPlayerId  uint32
	canTag          bool
	tagCoolDown     time.Duration
	cancelTagStart  context.CancelFunc
	cancelTagCheck  context.CancelFunc
}

func (room *Room) SendMessage(msg interface{}) {
	system.RLock()
	clients := append([]*Client(nil), room.Clients...)
	system.RUnlock()

	for _, c := range clients {
		c.SendMessage(msg)
	}
}

func (room *Room) SendMessageExcept(id uint32, msg interface{}) {
	system.RLock()
	clients := append([]*Client(nil), room.Clients...)
	system.RUnlock()

	for _, c := range clients {
		if c.Id != id {
			c.SendMessage(msg)
		}
	}
}

func (room *Room) stopTagTimersLocked() {
	if room.cancelTagStart != nil {
		room.cancelTagStart()
		room.cancelTagStart = nil
	}
	if room.cancelTagCheck != nil {
		room.cancelTagCheck()
		room.cancelTagCheck = nil
	}
}

func (room *Room) setTaggedPlayerLocked(taggedPlayerId uint32) {
	found := false
	for _, c := range room.Clients {
		if c.Id == taggedPlayerId {
			found = true
			break
		}
	}
	if !found {
		return
	}

	room.canTag = false
	room.taggedPlayerId = taggedPlayerId
	if room.tagCoolDown == 0 {
		room.tagCoolDown = 5 * time.Second
	}
	room.stopTagTimersLocked()

	room.SendMessage(map[string]interface{}{
		"type":           "tagged",
		"taggedPlayerId": taggedPlayerId,
		"coolDown":       int(room.tagCoolDown.Seconds()),
	})

	ctx, cancel := context.WithCancel(context.Background())
	room.cancelTagStart = cancel
	go func(d time.Duration) {
		t := time.NewTimer(d)
		defer t.Stop()
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			room.mu.Lock()
			room.canTag = true
			room.mu.Unlock()
			room.SendMessage(map[string]interface{}{"type": "canTag"})
		}
	}(room.tagCoolDown)
}

func (room *Room) tagRandomPlayerLocked() {
	if len(room.Clients) == 0 {
		return
	}
	room.setTaggedPlayerLocked(room.Clients[0].Id)
}

func (room *Room) StartTagGameMode() {
	room.mu.Lock()
	defer room.mu.Unlock()
	room.gameMode = TagGameMode
	room.SendMessage(map[string]interface{}{
		"type":     "gameMode",
		"gameMode": TagGameMode,
	})
	room.tagRandomPlayerLocked()
	log.Printf("room %q: tag started tagged=%x\n", room.Name, room.taggedPlayerId)
}

func (room *Room) EndGameMode() {
	room.mu.Lock()
	defer room.mu.Unlock()
	room.gameMode = ""
	room.taggedPlayerId = 0
	room.canTag = false
	room.stopTagTimersLocked()
	room.SendMessage(map[string]interface{}{
		"type":     "gameMode",
		"gameMode": "",
	})
	log.Printf("room %q: game mode ended\n", room.Name)
}

func (room *Room) SetTagCooldown(seconds float64) {
	if seconds < 1 {
		seconds = 1
	}
	if seconds > 60 {
		seconds = 60
	}
	room.mu.Lock()
	room.tagCoolDown = time.Duration(seconds * float64(time.Second))
	room.mu.Unlock()
}

func (room *Room) PlayerDied(player *Client) {
	if player == nil {
		return
	}
	room.mu.Lock()
	defer room.mu.Unlock()
	if room.gameMode != TagGameMode || player.Id == room.taggedPlayerId {
		return
	}
	room.setTaggedPlayerLocked(player.Id)
}

func (room *Room) onPlayerRemoved(player *Client) {
	room.mu.Lock()
	defer room.mu.Unlock()
	if room.gameMode == TagGameMode && player.Id == room.taggedPlayerId {
		room.stopTagTimersLocked()
		if len(room.Clients) > 0 {
			room.tagRandomPlayerLocked()
		} else {
			room.taggedPlayerId = 0
			room.canTag = false
			room.gameMode = ""
		}
	}
}

func (room *Room) checkTagTouch(mover *Client) {
	if mover == nil {
		return
	}
	room.mu.Lock()
	can := room.canTag && room.gameMode == TagGameMode
	taggedID := room.taggedPlayerId
	room.mu.Unlock()
	if !can || mover.Id == taggedID {
		return
	}

	mover.RLock()
	moverLevel := mover.Level
	moverPos := mover.Position
	moverHas := mover.HasPos
	mover.RUnlock()
	if !moverHas {
		return
	}

	tagged := system.GetClientById(taggedID)
	if tagged == nil {
		return
	}
	tagged.RLock()
	sameLevel := levelsMatch(tagged.Level, moverLevel)
	taggedPos := tagged.Position
	taggedHas := tagged.HasPos
	tagged.RUnlock()
	if !sameLevel || !taggedHas {
		return
	}
	if distanceMeters(moverPos, taggedPos) >= TagTouchMeters {
		return
	}

	room.mu.Lock()
	defer room.mu.Unlock()
	if !room.canTag || room.taggedPlayerId != taggedID {
		return
	}
	room.setTaggedPlayerLocked(mover.Id)
	log.Printf("room %q: tag touch id=%x\n", room.Name, mover.Id)
}

func (room *Room) HandleInteract(from *Client, toID uint32, kind string, clientDist float64) {
	if from == nil || from.Room == nil {
		return
	}
	if kind == "" {
		kind = "wave"
	}
	to := system.GetClientById(toID)
	if to == nil || to.Room != from.Room {
		return
	}

	from.RLock()
	fromLevel := from.Level
	fromPos := from.Position
	fromHas := from.HasPos
	from.RUnlock()
	to.RLock()
	toLevel := to.Level
	toPos := to.Position
	toHas := to.HasPos
	to.RUnlock()
	if fromLevel == "" || !levelsMatch(fromLevel, toLevel) {
		return
	}

	distOK := true
	if fromHas && toHas {
		distOK = distanceMeters(fromPos, toPos) <= InteractMaxMeters
	} else if clientDist > 0 {
		distOK = clientDist <= InteractMaxMeters
	}
	if !distOK {
		log.Printf("room %q: interact rejected from=%x to=%x (distance)\n",
			room.Name, from.Id, toID)
		return
	}

	msg := map[string]interface{}{
		"type": "interact",
		"from": from.Id,
		"to":   toID,
		"kind": kind,
	}
	room.SendMessage(msg)
	log.Printf("room %q: interact %s from=%x to=%x\n", room.Name, kind, from.Id, toID)
}

type System struct {
	sync.RWMutex
	Rooms       map[string]*Room
	ClientsById map[uint32]*Client
}

func (system *System) GetClientById(id uint32) *Client {
	system.RLock()
	defer system.RUnlock()
	return system.ClientsById[id]
}

func (system *System) AddClient(client *Client) {
	system.ClientsById[client.Id] = client
}

func (system *System) RemoveClient(id uint32) {
	delete(system.ClientsById, id)
}

var system = System{
	Rooms:       map[string]*Room{},
	ClientsById: map[uint32]*Client{},
}

func main() {
	log.Printf("Mirror's Edge multiplayer server starting on port %s...\n", getPort())

	go tcpListener()
	go udpListener()
	go discoveryListener()

	go func() {
		ticker := time.NewTicker(60 * time.Second)
		defer ticker.Stop()
		for range ticker.C {
			system.RLock()
			totalClients := 0
			for _, r := range system.Rooms {
				totalClients += len(r.Clients)
			}
			roomCount := len(system.Rooms)
			system.RUnlock()
			log.Printf("status: %d rooms, %d clients\n", roomCount, totalClients)
		}
	}()

	for {
		time.Sleep(HeartbeatInterval)

		system.RLock()
		rooms := make([]*Room, 0, len(system.Rooms))
		for _, r := range system.Rooms {
			rooms = append(rooms, r)
		}
		system.RUnlock()

		for _, r := range rooms {
			r.SendMessage(map[string]interface{}{
				"type": "ping",
			})
		}

		var newRooms = map[string]*Room{}

		system.Lock()
		for name, r := range system.Rooms {
			var newClients []*Client
			for _, c := range r.Clients {
				age := c.LastSeenAge()
				if age < ClientTimeout {
					newClients = append(newClients, c)
				} else {
					system.RemoveClient(c.Id)
					go c.CloseTcp()
					go r.SendMessageExcept(c.Id, map[string]interface{}{
						"type": "disconnect",
						"id":   c.Id,
					})

					log.Printf("timed out %x \"%s\" (last seen %s ago)\n",
						c.Id, c.Name, age.Truncate(time.Second))
				}
			}

			r.Clients = newClients
			if len(newClients) > 0 {
				newRooms[name] = r
			} else {
				log.Printf("deleted room \"%s\"\n", name)
			}
		}

		system.Rooms = newRooms
		system.Unlock()
	}
}

func getTrimStringField(obj map[string]interface{}, field string) (string, bool) {
	v, ok := obj[field].(string)
	if !ok {
		return "", false
	}

	v = strings.TrimSpace(v)
	return v, v != ""
}

func tcpHandler(c net.Conn) {
	defer c.Close()

	reader := bufio.NewReader(c)
	var sessionClient *Client

	defer func() {
		if sessionClient == nil {
			return
		}

		system.Lock()
		removeClientFromRoom(sessionClient)
		system.Unlock()
		log.Printf("room \"%s\": \"%s\" tcp closed\n", sessionClient.Room.Name,
			sessionClient.Name)
	}()

	for {
		payload, err := reader.ReadBytes(0)
		if err != nil {
			if sessionClient != nil {
				log.Printf("tcp read ended room=%q client=%q id=%x err=%v\n",
					sessionClient.Room.Name, sessionClient.Name, sessionClient.Id, err)
			} else {
				log.Printf("tcp read ended before session: %v\n", err)
			}
			break
		}

		payload = bytes.TrimRight(payload, "\x00")
		if len(payload) == 0 {
			continue
		}

		var msg map[string]interface{}
		if err := json.Unmarshal(payload, &msg); err != nil {
			continue
		}

		msgType, ok := getTrimStringField(msg, "type")
		if !ok {
			continue
		}

		switch msgType {
		case "connect":
			if sessionClient != nil {
				continue
			}

			msgRoom, ok := getTrimStringField(msg, "room")
			if !ok {
				continue
			}

			msgName, ok := getTrimStringField(msg, "name")
			if !ok {
				continue
			}

			msgLevel, ok := getTrimStringField(msg, "level")
			if !ok {
				continue
			}

			msgCharacter, ok := msg["character"].(float64)
			if !ok || msgCharacter < 0 || msgCharacter >= CharacterMax {
				continue
			}

			system.Lock()
			room, ok := system.Rooms[msgRoom]
			if !ok {
				room = &Room{
					Name: msgRoom,
				}

				system.Rooms[msgRoom] = room
				log.Printf("created room \"%s\"\n", room.Name)
			}

			client := &Client{
				Tcp:        c,
				Id:         uuid.New().ID(),
				Room:       room,
				Name:       msgName,
				Character:  uint32(msgCharacter),
				Level:      strings.ToLower(msgLevel),
				LastPacket: nil,
				LastSeen:   time.Now(),
			}

			room.Clients = append(room.Clients, client)
			system.AddClient(client)
			system.Unlock()

			sessionClient = client

			room.mu.Lock()
			gm := room.gameMode
			tagged := room.taggedPlayerId
			canTag := room.canTag
			room.mu.Unlock()

			client.SendMessage(map[string]interface{}{
				"type":           "id",
				"id":             client.Id,
				"gameMode":       gm,
				"taggedPlayerId": tagged,
				"canTag":         canTag,
			})

			room.SendMessageExcept(client.Id, map[string]interface{}{
				"type":      "connect",
				"id":        client.Id,
				"name":      client.Name,
				"character": client.Character,
				"level":     client.Level,
			})

			system.RLock()
			for _, existing := range room.Clients {
				if existing.Id != client.Id {
					client.SendMessage(map[string]interface{}{
						"type":      "connect",
						"id":        existing.Id,
						"name":      existing.Name,
						"character": existing.Character,
						"level":     existing.Level,
					})
				}
			}
			system.RUnlock()

			log.Printf("room \"%s\": \"%s\" joined (id=%x, level=%q)\n", room.Name, client.Name,
				client.Id, client.Level)
		case "name":
			id, ok := msg["id"].(float64)
			if !ok {
				continue
			}

			name, ok := getTrimStringField(msg, "name")
			if !ok {
				continue
			}

			client := system.GetClientById(uint32(id))
			if client == nil {
				continue
			}

			oldName := client.Name
			client.Name = name
			client.Touch()
			client.Room.SendMessageExcept(client.Id, map[string]interface{}{
				"type": "name",
				"id":   client.Id,
				"name": client.Name,
			})
			log.Printf("room %q: %q renamed to %q\n", client.Room.Name, oldName, name)
		case "chat":
			id, ok := msg["id"].(float64)
			if !ok {
				continue
			}

			body, ok := msg["body"].(string)
			if !ok {
				continue
			}

			client := system.GetClientById(uint32(id))
			if client == nil {
				continue
			}

			client.Touch()
			client.Room.SendMessage(map[string]interface{}{
				"type": "chat",
				"body": client.Name + ": " + body,
			})
			log.Printf("room %q: %q: %s\n", client.Room.Name, client.Name, body)
		case "level":
			id, ok := msg["id"].(float64)
			if !ok {
				log.Printf("level message missing id from %s: %#v\n", c.RemoteAddr(), msg)
				continue
			}

			level, ok := msg["level"].(string)
			if !ok {
				log.Printf("level message missing level from %s: %#v\n", c.RemoteAddr(), msg)
				continue
			}

			client := system.GetClientById(uint32(id))
			if client == nil {
				log.Printf("level message for unknown client id=%d level=%q from %s\n", uint32(id), level, c.RemoteAddr())
				continue
			}

			oldLevel := client.Level
			client.Level = strings.ToLower(level)
			client.Touch()
			client.Room.SendMessageExcept(client.Id, map[string]interface{}{
				"type":  "level",
				"id":    client.Id,
				"level": client.Level,
			})
			if oldLevel != client.Level {
				log.Printf("room %q: %q level %q -> %q\n", client.Room.Name, client.Name, oldLevel, client.Level)
			} else {
				log.Printf("room %q: %q level unchanged %q\n", client.Room.Name, client.Name, client.Level)
			}
		case "character":
			id, ok := msg["id"].(float64)
			if !ok {
				continue
			}

			character, ok := msg["character"].(float64)
			if !ok || character < 0 || character >= CharacterMax {
				continue
			}

			client := system.GetClientById(uint32(id))
			if client == nil {
				continue
			}

			client.Character = uint32(character)
			client.Touch()
			client.Room.SendMessageExcept(client.Id, map[string]interface{}{
				"type":      "character",
				"id":        client.Id,
				"character": client.Character,
			})
			log.Printf("room %q: %q changed character to %d\n",
				client.Room.Name, client.Name, client.Character)
		case "pong":
			id, ok := msg["id"].(float64)
			if !ok {
				continue
			}

			client := system.GetClientById(uint32(id))
			if client == nil {
				continue
			}

			client.Touch()
		case "client_ping":
			id, ok := msg["id"].(float64)
			if !ok {
				continue
			}

			ts, ok := msg["ts"].(float64)
			if !ok {
				continue
			}

			client := system.GetClientById(uint32(id))
			if client == nil {
				continue
			}

			client.Touch()
			client.SendMessage(map[string]interface{}{
				"type": "client_pong",
				"ts":   ts,
			})
		case "announce":
			if sessionClient == nil {
				continue
			}
			body, ok := msg["body"].(string)
			if !ok {
				continue
			}
			sessionClient.Touch()
			sessionClient.Room.SendMessage(map[string]interface{}{
				"type": "announce",
				"body": body,
			})
		case "cooldown":
			if sessionClient == nil {
				continue
			}
			cd, ok := msg["cooldown"].(float64)
			if !ok {
				continue
			}
			sessionClient.Touch()
			sessionClient.Room.SetTagCooldown(cd)
		case "startTagGameMode":
			if sessionClient == nil {
				continue
			}
			sessionClient.Touch()
			sessionClient.Room.StartTagGameMode()
		case "endGameMode":
			if sessionClient == nil {
				continue
			}
			sessionClient.Touch()
			sessionClient.Room.EndGameMode()
		case "dead":
			if sessionClient == nil {
				continue
			}
			sessionClient.Touch()
			sessionClient.Room.PlayerDied(sessionClient)
		case "interact":
			if sessionClient == nil {
				continue
			}
			toID, ok := msg["to"].(float64)
			if !ok {
				continue
			}
			kind, _ := getTrimStringField(msg, "kind")
			dist := 0.0
			if d, ok := msg["dist"].(float64); ok {
				dist = d
			}
			// Prefer authenticated session id over client-supplied "from".
			sessionClient.Touch()
			sessionClient.Room.HandleInteract(sessionClient, uint32(toID), kind, dist)
		case "disconnect":
			id, ok := msg["id"].(float64)
			if !ok {
				continue
			}

			client := system.GetClientById(uint32(id))
			if client == nil {
				continue
			}

			system.Lock()
			removeClientFromRoom(client)
			system.Unlock()
			sessionClient = nil

			log.Printf("room \"%s\": \"%s\" disconnected\n", client.Room.Name,
				client.Name)
			return
		}
	}
}

func discoveryListener() {
	conn, err := net.ListenPacket("udp4", ":"+getDiscoveryPort())
	if err != nil {
		log.Println("discovery listener failed:", err)
		return
	}
	defer conn.Close()

	log.Println("discovery listener started on port", getDiscoveryPort())

	buf := make([]byte, 512)
	for {
		n, addr, err := conn.ReadFrom(buf)
		if err != nil || n <= 0 {
			continue
		}

		var msg map[string]interface{}
		if err := json.Unmarshal(buf[:n], &msg); err != nil {
			continue
		}

		msgType, ok := getTrimStringField(msg, "type")
		if !ok || msgType != "discover" {
			continue
		}

		host, _, err := net.SplitHostPort(addr.String())
		if err != nil {
			host = addr.String()
		}

		log.Printf("discovery request from %s\n", host)

		response, err := json.Marshal(map[string]interface{}{
			"type": "announce",
			"host": host,
			"port": getPort(),
			"name": "Mirror's Edge Multiplayer",
		})
		if err != nil {
			continue
		}

		_, _ = conn.WriteTo(append(response, 0), addr)
	}
}

func tcpListener() {
	server, err := net.Listen("tcp4", ":"+getPort())
	if err != nil {
		log.Fatalln(err)
	}

	log.Println("tcp listener started on port", getPort())

	for {
		c, err := server.Accept()
		if err != nil {
			log.Printf("tcp accept error: %v\n", err)
			continue
		}

		log.Printf("tcp connection from %s\n", c.RemoteAddr().String())
		go tcpHandler(c)
	}
}

func udpListener() {
	server, err := net.ListenPacket("udp", ":"+getPort())
	if err != nil {
		log.Fatalln(err)
	}

	log.Println("udp listener started on port", getPort(), "(push-relay)")

	// Receive buffer must exceed PacketSize — Windows WSARecvFrom returns
	// WSAEMSGSIZE when the datagram is larger than the buffer (seen when a
	// stale server binary still used 676 while clients sent B3-lite 690).
	buf := make([]byte, 2048)
	unknownCount := 0
	pushCount := 0
	lastUdpLog := time.Now()
	loggedPushOnce := false
	oversizedCount := 0

	for {
		n, addr, err := server.ReadFrom(buf)
		if err != nil {
			log.Printf("udp read error: %v\n", err)
			continue
		}

		if n < PacketSizeLegacy || n > PacketSize {
			if n > PacketSize {
				oversizedCount++
				if time.Since(lastUdpLog) > 5*time.Second {
					log.Printf("udp: drop oversized n=%d (max=%d) count=%d\n",
						n, PacketSize, oversizedCount)
					oversizedCount = 0
					lastUdpLog = time.Now()
				}
			}
			continue
		}

		packetID := (*Packet)(unsafe.Pointer(&buf[0])).Id
		client := system.GetClientById(packetID)
		if client == nil {
			unknownCount++
			if time.Since(lastUdpLog) > 5*time.Second {
				log.Printf("udp: unknown=%d push=%d (last 5s)\n",
					unknownCount, pushCount)
				unknownCount = 0
				pushCount = 0
				lastUdpLog = time.Now()
			}
			continue
		}

		packet := make([]byte, n)
		copy(packet, buf[:n])

		client.Lock()
		client.LastPacket = packet
		client.UdpAddr = addr
		client.LastSeen = time.Now()
		if pos, ok := positionFromPacket(packet); ok {
			client.Position = pos
			client.HasPos = true
		}
		room := client.Room
		clientID := client.Id
		level := client.Level
		client.Unlock()

		if room == nil {
			continue
		}

		room.checkTagTouch(client)

		system.RLock()
		peers := append([]*Client(nil), room.Clients...)
		system.RUnlock()

		for _, peer := range peers {
			if peer.Id == clientID {
				continue
			}

			peer.RLock()
			sameLevel := levelsMatch(peer.Level, level)
			peerAddr := peer.UdpAddr
			peer.RUnlock()

			if !sameLevel || peerAddr == nil {
				continue
			}

			// Pure push-relay: forward sender's packet to peers. No pull-reply
			// (that doubled UDP vs legacy and stress-tested the game client).
			if _, err := server.WriteTo(packet, peerAddr); err == nil {
				pushCount++
				if !loggedPushOnce {
					loggedPushOnce = true
					log.Printf("udp: push-relay first forward bytes=%d\n", len(packet))
				}
			}
		}

		if time.Since(lastUdpLog) > 5*time.Second {
			log.Printf("udp: unknown=%d push=%d (last 5s)\n",
				unknownCount, pushCount)
			unknownCount = 0
			pushCount = 0
			lastUdpLog = time.Now()
		}
	}
}
