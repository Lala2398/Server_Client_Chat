# client_server_chat

A small multi-client chat application written in C for Linux, using BSD
sockets (TCP). A central **server** relays messages between any number of
**clients** (up to `MAX_CLIENTS`), so several people can talk in one room.

## How it works

```
   client (alice) ─┐
                   ├──► server (relay/hub) ──► broadcasts each message
   client (bob)   ─┘                          to all the OTHER clients
```

* **Server** — listens on a TCP port and accepts connections. Each client is
  handled in its own **thread**. A shared, mutex-protected list keeps track of
  who is online. When a message arrives from one client, the server
  **broadcasts** it to all the others, and prints join/leave/chat activity to
  its own console.
* **Client** — connects to the server, asks for a username, then runs two
  things at once: a background **thread** that prints incoming messages, and
  the main loop that reads your keyboard input and sends it.

## Project layout

```
client_server_chat/
├── include/common.h     shared settings (port, buffer size, max clients)
├── src/server.c         the chat hub
├── src/client.c         the chat client
├── Makefile             simple build (recommended)
├── CMakeLists.txt       optional CMake build
├── LICENSE
└── README.md
```

## Build

### Option A — Makefile (simplest)

```bash
make
```

This produces two programs in `./bin`: `bin/server` and `bin/client`.

To rebuild from scratch:

```bash
make clean && make
```

### Option B — CMake

```bash
mkdir -p build && cd build
cmake ..
make
```

This produces `build/server` and `build/client`.

## Run

Open **separate terminals** (one for the server, one per person chatting).

**Terminal 1 — start the server:**

```bash
./bin/server
```

You should see: `Chat server listening on port 8080 (max 10 clients).`

**Terminal 2 — first user:**

```bash
./bin/client
```

It will ask for a name, then you can type messages.

**Terminal 3 — second user:**

```bash
./bin/client
```

Now anything one person types appears on the other person's screen. Add more
terminals for more people (up to `MAX_CLIENTS`).

* Type `exit` (and press Enter) to leave the chat.
* Press `Ctrl+C` in the server terminal to shut the server down.

### Chatting between two different computers

By default the client connects to `127.0.0.1` (the same machine). To connect to
a server running on another machine on the network, pass its IP address:

```bash
./bin/client 192.168.1.42
```

(Make sure the port — `8080` by default — is reachable / not blocked by a
firewall.)

## Configuration

Edit `include/common.h` to change the defaults, then rebuild:

| Setting        | Meaning                                   | Default |
|----------------|-------------------------------------------|---------|
| `PORT`         | TCP port the server listens on            | `8080`  |
| `BUFFER_SIZE`  | Max bytes per message                     | `2048`  |
| `MAX_CLIENTS`  | Max simultaneous users                    | `10`    |
| `NAME_LEN`     | Max username length                       | `32`    |
