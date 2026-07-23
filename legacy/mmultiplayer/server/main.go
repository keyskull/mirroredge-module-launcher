package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"log"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"unsafe"

	"github.com/google/uuid"
)

func init() {
	logPath := filepath.Join(os.TempDir(), "mmultiplayer-server.log")
	if f, err := os.OpenFile(logPath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644); err == nil {
		log.SetOutput(f)
		log.SetFlags(log.LstdFlags | log.Lmicroseconds)
	}
}

const (
	DefaultPort           = "5222"
	DefaultDiscoveryPort  = "5223"
	PacketSize            = 676
	CharacterFaith = iota
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

type Packet struct {
	Id uint32
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
		client.Tcp.Write(payload)
	}
}

func removeClientFromRoom(client *Client) {
	if client == nil || client.Room == nil {
		return
	}

	room := client.Room
	var newClients []*Client
	for _, c := range room.Clients {
		if c.Id != client.Id {
			newClients = append(newClients, c)
		}
	}

	room.Clients = newClients
	system.RemoveClient(client.Id)

	if len(newClients) > 0 {
		room.SendMessageExcept(client.Id, map[string]interface{}{
			"type": "disconnect",
			"id":   client.Id,
		})
	}
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
	Name    string
	Clients []*Client
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
	go tcpListener()
	go udpListener()
	go discoveryListener()

	for {
		time.Sleep(2 * time.Second)

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
				if time.Since(c.LastSeen) < 5*time.Second {
					newClients = append(newClients, c)
				} else {
					system.RemoveClient(c.Id)
					go r.SendMessageExcept(c.Id, map[string]interface{}{
						"type": "disconnect",
						"id":   c.Id,
					})

					log.Printf("timed out %x \"%s\"\n", c.Id, c.Name)
				}
			}

			if len(newClients) > 0 {
				r.Clients = newClients
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

			client.SendMessage(map[string]interface{}{
				"type":           "id",
				"id":             client.Id,
				"gameMode":       "",
				"taggedPlayerId": 0,
				"canTag":         false,
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

			log.Printf("room \"%s\": \"%s\" joined (id=%x)\n", room.Name, client.Name,
				client.Id)
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

			client.Name = name
			client.LastSeen = time.Now()
			client.Room.SendMessageExcept(client.Id, map[string]interface{}{
				"type": "name",
				"id":   client.Id,
				"name": client.Name,
			})
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

			client.LastSeen = time.Now()
			client.Room.SendMessage(map[string]interface{}{
				"type": "chat",
				"body": client.Name + ": " + body,
			})
		case "level":
			id, ok := msg["id"].(float64)
			if !ok {
				continue
			}

			level, ok := msg["level"].(string)
			if !ok {
				continue
			}

			client := system.GetClientById(uint32(id))
			if client == nil {
				continue
			}

			client.Level = strings.ToLower(level)
			client.LastSeen = time.Now()
			client.Room.SendMessageExcept(client.Id, map[string]interface{}{
				"type":  "level",
				"id":    client.Id,
				"level": client.Level,
			})
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
			client.LastSeen = time.Now()
			client.Room.SendMessageExcept(client.Id, map[string]interface{}{
				"type":      "character",
				"id":        client.Id,
				"character": client.Character,
			})
		case "pong":
			id, ok := msg["id"].(float64)
			if !ok {
				continue
			}

			client := system.GetClientById(uint32(id))
			if client == nil {
				continue
			}

			client.LastSeen = time.Now()
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

			client.LastSeen = time.Now()
			client.SendMessage(map[string]interface{}{
				"type": "client_pong",
				"ts":   ts,
			})
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
			continue
		}

		go tcpHandler(c)
	}
}

func udpListener() {
	server, err := net.ListenPacket("udp", ":"+getPort())
	if err != nil {
		log.Fatalln(err)
	}

	log.Println("udp listener started on port", getPort())

	buf := make([]byte, PacketSize)
	for {
		n, addr, err := server.ReadFrom(buf)
		if err != nil {
			continue
		}

		if n != PacketSize {
			continue
		}

		packetID := (*Packet)(unsafe.Pointer(&buf[0])).Id
		client := system.GetClientById(packetID)
		if client == nil {
			continue
		}

		packet := make([]byte, PacketSize)
		copy(packet, buf)

		client.Lock()
		client.LastPacket = packet
		client.LastSeen = time.Now()
		room := client.Room
		clientID := client.Id
		level := client.Level
		client.Unlock()

		if room == nil {
			continue
		}

		system.RLock()
		peers := append([]*Client(nil), room.Clients...)
		system.RUnlock()

		for _, peer := range peers {
			if peer.Id == clientID {
				continue
			}

			peer.RLock()
			sameLevel := peer.Level == level
			lastPacket := peer.LastPacket
			peer.RUnlock()

			if sameLevel && lastPacket != nil {
				_, _ = server.WriteTo(lastPacket, addr)
			}
		}
	}
}
