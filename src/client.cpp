// client program

#include <cerrno>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>
#include "socket_utils.h"
#include "crypto_utils.h"
#include "protocol.h"
#include <SFML/Graphics.hpp>
#include <cstring>

using namespace std;

namespace
{
constexpr size_t MAX_MESSAGE_SIZE = MAX_CHAT_MESSAGE_SIZE;
atomic<bool> connection_alive{true};
mutex chat_messages_mutex;
vector<string> chat_messages;

bool send_message(int socket_fd, const string& message)
{
    if (message.empty() || message.size() > MAX_MESSAGE_SIZE)
        return false;

    char frame[MESSAGE_BUFFER_SIZE];
    memcpy(frame, message.data(), message.size());
    frame[message.size()] = '\0';
    encrypt(frame, KEY_VALUE);
    frame[message.size()] = '\n';

    size_t sent = 0;
    const size_t frame_size = message.size() + 1;
    while (sent < frame_size)
    {
        ssize_t result = send(socket_fd, frame + sent, frame_size - sent, MSG_NOSIGNAL);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return false;
        sent += static_cast<size_t>(result);
    }

    return true;
}

void receive_messages(int socket_fd)
{
    char buffer[512];
    string pending;
    bool dropping_oversized_message = false;

    while (true)
    {
        ssize_t bytes_read = recv(socket_fd, buffer, sizeof(buffer), 0);
        if (bytes_read < 0 && errno == EINTR)
            continue;
        if (bytes_read <= 0)
            break;

        for (ssize_t index = 0; index < bytes_read; ++index)
        {
            const char character = buffer[index];
            if (character == '\n')
            {
                if (!dropping_oversized_message && !pending.empty())
                {
                    if (pending.back() == '\r')
                        pending.pop_back();
                    if (!pending.empty())
                    {
                        pending.push_back('\0');
                        decrypt(pending.data(), KEY_VALUE);
                        pending.pop_back();
                        lock_guard<mutex> lock(chat_messages_mutex);
                        chat_messages.push_back(pending);
                    }
                }
                pending.clear();
                dropping_oversized_message = false;
            }
            else if (!dropping_oversized_message)
            {
                if (character == '\0' || pending.size() == MAX_MESSAGE_SIZE)
                {
                    pending.clear();
                    dropping_oversized_message = true;
                }
                else
                    pending.push_back(character);
            }
        }
    }
    connection_alive.store(false);
}

bool parse_port(const char* value, int& port)
{
    char* end = nullptr;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 1 || parsed > 65535)
        return false;
    port = static_cast<int>(parsed);
    return true;
}

void chat_gui(int socket_fd, const string& name)
{
    sf::RenderWindow window(sf::VideoMode({600, 400}), "Parlons");
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.openFromFile("assets/Lato-Regular.ttf"))
    {
        cerr << "Failed to load font file\n";
        return;
    }

    sf::Text input_text(font);
    input_text.setCharacterSize(18);
    input_text.setFillColor(sf::Color::White);
    input_text.setPosition({10.f, 360.f});

    sf::RectangleShape message_box(sf::Vector2f(580, 30));
    message_box.setPosition({10.f, 355.f});
    message_box.setFillColor(sf::Color(50, 50, 50));

    string current_input;
    int cursor_frame = 0;

    while (window.isOpen() && connection_alive.load())
    {
        while (const auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
                window.close();

            if (const auto* text_event = event->getIf<sf::Event::TextEntered>())
            {
                if ((text_event->unicode == '\b' || text_event->unicode == 127) && !current_input.empty())
                    current_input.pop_back();
                else if (text_event->unicode == '\r' || text_event->unicode == '\n')
                {
                    if (!current_input.empty())
                    {
                        string outgoing_message = name + " - " + current_input;
                        if (outgoing_message.size() > MAX_MESSAGE_SIZE)
                            cerr << "Message is too long to send\n";
                        else if (!send_message(socket_fd, outgoing_message))
                        {
                            cerr << "Connection lost while sending message\n";
                            window.close();
                        }
                        else
                        {
                            lock_guard<mutex> lock(chat_messages_mutex);
                            chat_messages.push_back("Me - " + current_input);
                            current_input.clear();
                        }
                    }
                }
                else if (text_event->unicode >= 32 && text_event->unicode < 127 &&
                         name.size() + 3 + current_input.size() < MAX_MESSAGE_SIZE)
                    current_input += static_cast<char>(text_event->unicode);
            }
        }

        window.clear(sf::Color(30, 30, 30));
        window.draw(message_box);

        {
            lock_guard<mutex> lock(chat_messages_mutex);
            const size_t visible_count = 14;
            const size_t first_message = chat_messages.size() > visible_count
                ? chat_messages.size() - visible_count : 0;
            float y = 0.f;
            for (size_t index = first_message; index < chat_messages.size(); ++index)
            {
                sf::Text message(font);
                message.setString(chat_messages[index]);
                message.setCharacterSize(18);
                message.setFillColor(sf::Color(255, 192, 203));
                message.setPosition({10.f, y});
                y += 25.f;
                window.draw(message);
            }
        }

        if (cursor_frame++ < 30)
            input_text.setString(current_input + "|");
        else if (cursor_frame < 60)
            input_text.setString(current_input);
        else
            cursor_frame = 0;

        window.draw(input_text);
        window.display();
    }
}
}

int main(int argc, char** argv)
{
    string ip = "127.0.0.1";
    int port = 2000;
    string name;

    if (argc == 4)
    {
        ip = argv[1];
        name = argv[3];
        if (!parse_port(argv[2], port))
        {
            cerr << "Port must be between 1 and 65535\n";
            return 1;
        }
    }
    else if (argc == 1)
    {
        cout << "Enter Client Name: ";
        if (!getline(cin, name))
            return 1;
    }
    else
    {
        cerr << "Usage: " << argv[0] << " [server-ip port name]\n";
        return 1;
    }

    if (name.empty() || name.size() + 4 > MAX_MESSAGE_SIZE ||
        name.find_first_of("\r\n") != string::npos)
    {
        cerr << "Client name is empty, too long, or contains a line break\n";
        return 1;
    }

    int socket_fd = createTCPIpv4Socket();
    if (socket_fd < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr* address = createTCPIpv4SocketAddress(ip.c_str(), port);
    if (address == nullptr)
    {
        cerr << "Invalid server address\n";
        close(socket_fd);
        return 1;
    }

    if (connect(socket_fd, address, sizeof(struct sockaddr_in)) < 0)
    {
        perror("connect");
        free(address);
        close(socket_fd);
        return 1;
    }
    free(address);
    cout << "Connection Successful\n";

    thread receiver(receive_messages, socket_fd);
    chat_gui(socket_fd, name);

    shutdown(socket_fd, SHUT_RDWR);
    close(socket_fd);
    receiver.join();
    return 0;
}


