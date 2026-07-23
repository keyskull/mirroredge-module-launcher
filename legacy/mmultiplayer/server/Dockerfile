FROM golang:1.21-alpine AS builder
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY main.go .
RUN CGO_ENABLED=0 GOOS=linux go build \
    -ldflags "-w -s" \
    -tags netgo -installsuffix netgo \
    -o /server .

FROM scratch
COPY --from=builder /server /server
ENTRYPOINT [ "/server" ]
