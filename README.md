# Socket Relay - LAN Chat System

A multi-client chat application built with C/C++ that uses TCP sockets to relay encrypted messages between clients through a central server. The server acts as a message hub, receiving messages from one client and broadcasting them to all other connected clients, while each client provides a graphical user interface (GUI) using SFML for real-time message display and input.

## Key Features

- **Multi-Client Support**: Central server manages multiple concurrent client connections using pthreads
- **Message Broadcasting**: Messages from any client are relayed to all other connected clients
- **Encryption**: Caesar cipher encryption (shift-4) protects messages in transit
- **Graphical Interface**: Real-time chat GUI built with SFML (Simple and Fast Multimedia Library)
- **Thread-Safe Operations**: Mutex locks and semaphores ensure safe concurrent access to shared resources
- **Message Queuing**: Bounded message queue (50 messages max) on the server side for orderly message processing

---

## System Architecture

### Overview

```
   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
   │   CLIENT 1   │   │   CLIENT 2   │   │   CLIENT N   │
   │  client.cpp  │   │  client.cpp  │   │  client.cpp  │
   │  (SFML GUI)  │   │  (SFML GUI)  │   │  (SFML GUI)  │
   └──────┬───────┘   └──────┬───────┘   └──────┬───────┘
          │ TCP              │ TCP              │ TCP
          └──────────────────┼──────────────────┘
                             │
                   ┌─────────┴──────────┐
                   │   CENTRAL SERVER   │
                   │      server.c      │
                   │   TCP port 2000    │
                   └────────────────────┘
```

All connections are IPv4 TCP sockets.

### Server Internals

```
 Client A ──TCP──> ┌──────────────────┐
 Client B ──TCP──> │  Handler Threads │   one per client (producers)
 Client N ──TCP──> └────────┬─────────┘
                            │ enqueue
                            ▼
                   ┌──────────────────┐
                   │  Message Queue   │   circular, max 50 messages
                   └────────┬─────────┘
                            │ dequeue
                            ▼
                   ┌──────────────────┐         ┌──> Client B
                   │   Send Thread    │──TCP────┼──> Client C
                   └──────────────────┘         └──> Client N
                                            (all clients except the sender)

 Accept Thread: listens on port 2000 and spawns one Handler Thread
                for every new client connection.
```

### Client Internals

| Component | Runs in | Responsibility |
|-----------|---------|----------------|
| GUI | Main thread | SFML window captures input and renders the chat |
| Sender | Main thread | Adds the client name, encrypts the frame, and sends it over TCP |
| Receiver | `std::thread` | Reassembles newline-delimited frames, decrypts messages, and updates the chat |

---

## Data Flow

### Sending a message (Client A to other clients)

```
CLIENT A (sender)
    1. GUI               TextEntered events build current_input = "Hello"
    2. GUI               Enter pressed: create "ClientA - Hello"
    3. Client            Display local echo as "Me - Hello"
    4. Client            Encrypt (Caesar, KEY=4) and send a newline-delimited TCP frame
        │
        ▼  TCP
SERVER
    5. Handler thread   Reassemble complete newline-delimited frames
                      sem_wait(&empty)        wait for free queue slot
                      sem_wait(&queue_mutex)  lock queue
                      enqueue(msg, client_fd)
                      sem_post(&queue_mutex)  unlock queue
                      sem_post(&full)         signal a message is available
  6. Send thread      sem_wait(&full)         wait for a queued message
                      sem_wait(&queue_mutex)  lock queue
                      dequeue(&src_fd)        get message + sender fd
                      sem_post(&queue_mutex)  unlock queue
                      sem_post(&empty)        signal a slot is free
                      for each connected client_fd != src_fd:
                          send the complete encrypted frame
        │
        ▼  TCP
CLIENTS B, C, ... N (receivers)
    7. Receiver thread  Reassemble frame -> decrypt (KEY=4)
    8. GUI               Render the plaintext message
```

---

## Project Structure

```
socket-relay/
├── Makefile
├── README.md
├── .gitignore
├── include/
│   ├── crypto_utils.h
│   ├── data_structs.h
│   ├── protocol.h
│   └── socket_utils.h
├── src/
│   ├── client.cpp
│   ├── crypto_utils.c
│   ├── data_structs.c
│   ├── server.c
│   └── socket_utils.c
├── assets/
│   └── Lato-Regular.ttf
└── build/                  Generated binaries and object files (ignored)
    ├── client
    ├── server
    └── obj/
```

---

## How It Works

### Server Behavior

1. **Initialization**: Creates a TCP socket listening on port 2000.
2. **Accept Thread**: Continuously accepts new client connections (backlog of 10).
3. **Client Handlers**: For each connected client, a dedicated thread:
   - Reads messages from the client socket
   - Adds messages to the bounded queue (with semaphore synchronization)
4. **Send Thread**: A single broadcaster thread:
   - Monitors the queue using semaphores
   - Dequeues messages along with the sender's socket FD
   - Broadcasts to all connected clients except the sender

### Client Behavior

1. **Connection**: Connects to the server at `127.0.0.1:2000` by default.
2. **Sending**: Adds the client name, displays a local `Me - message` echo, encrypts the frame, and sends it to the server.
3. **Receiving**: A receiver thread assembles complete frames, decrypts messages, and adds them to the chat.
4. **GUI Rendering**:
   - Displays all messages in a scrollable text area
   - Shows an input box at the bottom with a blinking cursor
   - Updates at 60 FPS

### Message Queue (Server-Side)

- **Structure**: Circular queue holding up to 50 messages together with their sender socket FDs
- **Synchronization**:
  - `queue_mutex` (binary semaphore): protects queue access
  - `empty` (counting semaphore): tracks available slots (starts at 50)
  - `full` (counting semaphore): tracks queued messages (starts at 0)
- **Producers**: Client handler threads enqueue messages
- **Consumer**: A single send thread dequeues and broadcasts

---

## Setup Instructions

### Prerequisites

#### 1. GCC/G++ Compiler

**Ubuntu/Debian**
```bash
sudo apt-get update
sudo apt-get install build-essential gcc g++
```

**macOS (Homebrew)**
```bash
brew install gcc
```

**Fedora/RHEL**
```bash
sudo dnf groupinstall "Development Tools"
```

**Verify installation**
```bash
gcc --version
g++ --version
```

#### 2. SFML Library (v2.5.1 or later)

SFML is required for the GUI components.

**Ubuntu/Debian**
```bash
sudo apt-get install libsfml-dev
```

**macOS**
```bash
brew install sfml
```

**Fedora/RHEL**
```bash
sudo dnf install SFML-devel
```

**Manual installation (if no package is available)**
```bash
# Download SFML source
wget https://github.com/SFML/SFML/archive/2.6.0.tar.gz
tar xzf 2.6.0.tar.gz
cd SFML-2.6.0

# Build and install
mkdir build
cd build
cmake ..
make
sudo make install

# On Linux, update the library cache
sudo ldconfig
```

#### 3. POSIX Threading Library

Typically included with GCC on most systems. Verify with:

```bash
cat /usr/include/pthread.h | head -20
```

#### 4. Standard Build Tools

```bash
# Ubuntu/Debian
sudo apt-get install make

# Fedora/RHEL
sudo dnf install make

# macOS
brew install make
```

### Verify Dependencies

Run this script to check that everything is installed:

```bash
#!/bin/bash
echo "=== Checking Dependencies ==="

# Check GCC
if command -v gcc &> /dev/null; then
    echo "✓ GCC found: $(gcc --version | head -1)"
else
    echo "✗ GCC not found"
fi

# Check G++
if command -v g++ &> /dev/null; then
    echo "✓ G++ found: $(g++ --version | head -1)"
else
    echo "✗ G++ not found"
fi

# Check Make
if command -v make &> /dev/null; then
    echo "✓ Make found: $(make --version | head -1)"
else
    echo "✗ Make not found"
fi

# Check SFML
if pkg-config --exists sfml-all; then
    echo "✓ SFML found: $(pkg-config --modversion sfml-graphics)"
else
    echo "✗ SFML not found (try: sudo apt-get install libsfml-dev)"
fi

# Check pthread
if grep -q "PTHREAD_MUTEX" /usr/include/pthread.h 2>/dev/null; then
    echo "✓ POSIX Threads available"
else
    echo "✗ POSIX Threads not found"
fi
```

### Clone and Build

#### Step 1: Clone the Repository

```bash
git clone https://github.com/zain-anwer/socket-relay.git
cd socket-relay
```

#### Step 2: Build the Project

**Using Make (recommended)**
```bash
make clean
make
```

This produces:

- `build/server` - Multi-client relay server
- `build/client` - Chat client with GUI

#### Step 3: Verify the Build

```bash
ls -la build/server build/client
```

### Run the Chat

Run the server and clients from the repository root. The client loads its font from `assets/Lato-Regular.ttf`, so launching it from another working directory will fail to find the font.

1. In Terminal 1, start the server and leave it running:

    ```bash
    ./build/server
    ```

    It listens on port `2000` by default and prints `Listening on port 2000`.

2. In Terminal 2, launch a client and enter a name when prompted:

    ```bash
    ./build/client
    ```

3. In Terminal 3, launch another client the same way. Repeat in additional terminals for more users. Each client needs a working graphical display.

    You can also pass the server address, port, and client name directly:

    ```bash
    ./build/client 127.0.0.1 2000 Alice
    ```

    For a server on another machine, replace `127.0.0.1` with its reachable IP address.

4. Type a message in a client window and press **Enter**. The sender sees `Me - Message`; the other connected clients see `ClientName - Message`.

Close a client window to disconnect. Stop the server with **Ctrl+C**. To use a non-default port, start the server with that port and pass the same port to every client, for example:

```bash
# Terminal 1
./build/server 2001

# Terminals 2 and 3
./build/client 127.0.0.1 2001 Alice
./build/client 127.0.0.1 2001 Bob
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `socket_utils.o: No such file or directory` | Run `make clean`, then `make` |
| `cannot find -lsfml-graphics` | Install SFML: `sudo apt-get install libsfml-dev` |
| `error: 'pthread_mutex_t' not recognized` | Compile with the `-pthread` flag |
| `Connection refused` | Make sure the server is running on the correct port (2000) |
| Font file not found | Make sure `assets/Lato-Regular.ttf` exists |
| X11 display error on a headless system | Use X11 forwarding or run on a system with a display |
| Port 2000 already in use | Start the server on another port (for example, `./build/server 2001`) and use the same port when starting clients |

## Limitations and Known Issues

1. **Encryption**: Uses a simple Caesar cipher (shift-4). Not suitable for production; use TLS/SSL for real applications.
2. **Message Queue**: Bounded at 50 messages. If exceeded, oldest messages are dropped.
3. **No Persistence**: Messages are not stored; only live connections receive chat history.
4. **Single Font**: Requires `assets/Lato-Regular.ttf` for the GUI.
5. **IPv4 Only**: No IPv6 support.
6. **No Username Registration**: The same client name can be used by multiple users.
7. **Basic GUI**: The SFML window is functional but minimal. No emoji, formatting, or advanced features.

## Performance Tuning

For high message throughput, consider:

1. **Increase queue size**: modify `QUEUE_SIZE` in `include/data_structs.h` (default: 50)
2. **Increase buffer size**: modify `MESSAGE_BUFFER_SIZE` in `include/protocol.h` (default: 200 bytes)
3. **Server backlog**: modify the listen backlog in `src/server.c` (default: 10)

Example:

```c
// include/data_structs.h
#define QUEUE_SIZE 100      // increased from 50

// include/protocol.h
#define MESSAGE_BUFFER_SIZE 512
```

Then rebuild:

```bash
make clean
make
```

## Clean Build

To remove all build artifacts:

```bash
make clean
```

This removes the generated `build/` directory and leaves source files untouched.

---

## Technical Stack

| Component | Technology | Notes |
|-----------|------------|-------|
| **Language** | C (server), C++ (client) | 66.6% C++, 32.3% C, 1.1% Makefile |
| **Networking** | BSD Sockets (IPv4) | TCP / `SOCK_STREAM` on port 2000 |
| **Threading** | POSIX Threads (pthreads) | Multiple threads per process |
| **Synchronization** | Semaphores + Mutexes | Producer-consumer queue pattern |
| **GUI** | SFML 3 | OpenGL-based graphics |
| **Encryption** | Caesar cipher | Simple shift-4 cipher (`KEY_VALUE=4`) |
| **Client concurrency** | C++ threads | GUI and socket receiver |
| **Build System** | GNU Make | Simple Makefile with gcc/g++ |

## Compilation Flags Explained

The Makefile adds `include/` to the header search path, compiles with pthread support and C++17, and links the client against SFML 3.

---

## Contributing

To contribute improvements:

1. Fork the repository.
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes and test thoroughly.
4. Commit: `git commit -am "Add my feature"`
5. Push: `git push origin feature/my-feature`
6. Submit a pull request.

### Suggested Improvements

- [ ] Implement TLS/SSL encryption
- [ ] Add SQLite message persistence
- [ ] Support IPv6
- [ ] Build a web-based client
- [ ] Add user authentication
- [ ] Implement message history retrieval
- [ ] Enhanced GUI with file sharing
- [ ] Support for private messages
- [ ] Message timestamps and read receipts

---

## License

This project is open source. See the repository for license details.

---

## References

- [POSIX Threads Programming](https://computing.llnl.gov/tutorials/pthreads/)
- [SFML Documentation](https://www.sfml-dev.org/documentation/2.5.1/)
- [BSD Socket Programming](https://beej.us/guide/bgnet/)
- [Semaphore Usage in C](https://www.man7.org/linux/man-pages/man3/sem_open.3.html)
- [Caesar Cipher](https://en.wikipedia.org/wiki/Caesar_cipher)

---

**Last Updated:** September 30, 2025  
**Repository:** https://github.com/zain-anwer/socket-relay  
**Author:** Zain Ul Abidin
