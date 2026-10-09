// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <poll.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <array>
#include <vector>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include "IClient.h"

namespace skyline::service::socket {
    namespace {
        // The guest uses BSD constants/layouts (but Linux errno values), so descriptors, flags and socket addresses need translating to the host ones

        constexpr i32 BsdEbadf{EBADF};
        constexpr i32 BsdEafnosupport{EAFNOSUPPORT};

        constexpr size_t MaxGuestFds{1024};
        std::mutex fdMutex;
        std::array<i32, MaxGuestFds> hostFds = [] {
            std::array<i32, MaxGuestFds> fds;
            fds.fill(-1);
            return fds;
        }(); //!< Maps the descriptors the guest sees (the index) to the host descriptors, the guest must never be able to touch host descriptors it doesn't own

        /**
         * @brief Allocates the lowest free guest descriptor for the given host descriptor
         * @return The guest descriptor or -1 if there are none left
         */
        i32 AllocateGuestFd(i32 hostFd) {
            std::scoped_lock lock{fdMutex};
            for (size_t i{}; i < hostFds.size(); i++) {
                if (hostFds[i] == -1) {
                    hostFds[i] = hostFd;
                    return static_cast<i32>(i);
                }
            }
            return -1;
        }

        /**
         * @return The host descriptor for the given guest descriptor or -1 if it isn't valid
         */
        i32 GetHostFd(i32 guestFd) {
            std::scoped_lock lock{fdMutex};
            if (guestFd < 0 || static_cast<size_t>(guestFd) >= hostFds.size())
                return -1;
            return hostFds[static_cast<size_t>(guestFd)];
        }

        /**
         * @brief Frees the given guest descriptor
         * @return The host descriptor that was mapped to it or -1 if it wasn't valid
         */
        i32 ReleaseGuestFd(i32 guestFd) {
            std::scoped_lock lock{fdMutex};
            if (guestFd < 0 || static_cast<size_t>(guestFd) >= hostFds.size())
                return -1;
            i32 hostFd{hostFds[static_cast<size_t>(guestFd)]};
            hostFds[static_cast<size_t>(guestFd)] = -1;
            return hostFd;
        }

        /**
         * @brief Converts a host errno to the value the guest expects
         * @note Even though the Switch uses the FreeBSD network stack its errno values are the Linux ones (https://switchbrew.org/wiki/Sockets_services), so the host value is passed through unchanged
         */
        i32 ToBsdErrno(i32 hostErrno) {
            return hostErrno;
        }

        i32 ToHostMsgFlags(i32 bsdFlags) {
            i32 flags{};
            if (bsdFlags & 0x1)
                flags |= MSG_OOB;
            if (bsdFlags & 0x2)
                flags |= MSG_PEEK;
            if (bsdFlags & 0x40)
                flags |= MSG_WAITALL;
            if (bsdFlags & 0x80)
                flags |= MSG_DONTWAIT;
            return flags;
        }

        i32 ToHostDomain(i32 bsdDomain) {
            return bsdDomain == 28 ? AF_INET6 : bsdDomain; // AF_INET6 is 28 on BSD but 10 on Linux, the rest are the same
        }

        /**
         * @brief Converts a BSD sockaddr_in (u8 length, u8 family, u16 port, u32 address, 8 bytes of padding) to the host one, only IPv4 is supported
         */
        bool GuestToHostAddress(span<u8> guestAddress, sockaddr_in &hostAddress) {
            if (guestAddress.size() < 8 || guestAddress[1] != 2)
                return false;

            hostAddress = {};
            hostAddress.sin_family = AF_INET;
            std::memcpy(&hostAddress.sin_port, guestAddress.data() + 2, sizeof(hostAddress.sin_port));
            std::memcpy(&hostAddress.sin_addr, guestAddress.data() + 4, sizeof(hostAddress.sin_addr));
            return true;
        }

        /**
         * @brief Writes a host sockaddr_in to the guest buffer using the BSD layout
         * @return The size of the address in bytes
         */
        u32 HostToGuestAddress(const sockaddr_in &hostAddress, span<u8> guestAddress) {
            std::array<u8, 16> bsdAddress{};
            bsdAddress[0] = static_cast<u8>(bsdAddress.size());
            bsdAddress[1] = 2; // AF_INET
            std::memcpy(bsdAddress.data() + 2, &hostAddress.sin_port, sizeof(hostAddress.sin_port));
            std::memcpy(bsdAddress.data() + 4, &hostAddress.sin_addr, sizeof(hostAddress.sin_addr));

            std::memcpy(guestAddress.data(), bsdAddress.data(), std::min(guestAddress.size(), bsdAddress.size()));
            return static_cast<u32>(bsdAddress.size());
        }

        constexpr i32 BsdOnonblock{0x4}; //!< O_NONBLOCK is 0x4 on BSD but 0x800 on Linux
    }

    IClient::IClient(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

    Result IClient::RegisterClient(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<i32>(0);
        return {};
    }

    Result IClient::StartMonitoring(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    Result IClient::Socket(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 domain{request.Pop<i32>()};
        i32 type{request.Pop<i32>()};
        i32 protocol{request.Pop<i32>()};

        i32 hostFd{::socket(ToHostDomain(domain), type, protocol)};
        if (hostFd == -1) {
            LOGW("Error creating socket: {}", strerror(errno));
            return PushBsdResult(response, -1, ToBsdErrno(errno));
        }

        i32 guestFd{AllocateGuestFd(hostFd)};
        if (guestFd == -1) {
            ::close(hostFd);
            return PushBsdResult(response, -1, 24); // EMFILE
        }

        LOGI("File Descriptor {} (host {}) with Domain {}, Type {}, Protocol {}", guestFd, hostFd, domain, type, protocol);
        return PushBsdResult(response, guestFd, 0);
    }

    Result IClient::Select(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        // Not implemented: report that no descriptor is ready and sleep briefly so a guest that calls this in a loop doesn't make a host core spin at 100%
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return PushBsdResult(response, 0, 0);
    }

    Result IClient::Poll(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fdsCount{request.Pop<i32>()};
        i32 timeout{request.Pop<i32>()};

        if (fdsCount <= 0)
            return PushBsdResult(response, -1, 22); // EINVAL

        span outputBuf{request.outputBuf.at(0)};
        if (outputBuf.size() < static_cast<size_t>(fdsCount) * sizeof(pollfd))
            return PushBsdResult(response, -1, 22); // EINVAL

        auto guestFds{span<pollfd>(reinterpret_cast<pollfd *>(outputBuf.data()), static_cast<size_t>(fdsCount))};

        // The guest descriptors have to be mapped to host ones, invalid ones are ignored by poll() when they're negative and reported as POLLNVAL
        std::vector<pollfd> hostPollFds(guestFds.size());
        std::vector<bool> invalid(guestFds.size());
        for (size_t i{}; i < guestFds.size(); i++) {
            i32 hostFd{GetHostFd(guestFds[i].fd)};
            invalid[i] = hostFd == -1;
            hostPollFds[i] = {.fd = hostFd, .events = guestFds[i].events, .revents = 0};
        }

        i32 result{poll(hostPollFds.data(), static_cast<nfds_t>(hostPollFds.size()), timeout)};
        i32 pollErrno{errno};

        for (size_t i{}; i < guestFds.size(); i++)
            guestFds[i].revents = invalid[i] ? static_cast<i16>(POLLNVAL) : hostPollFds[i].revents;

        return result == -1 ? PushBsdResult(response, -1, ToBsdErrno(pollErrno)) : PushBsdResult(response, result, 0);
    }

    Result IClient::Recv(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        i32 flags{request.Pop<i32>()};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        ssize_t result{recv(fd, request.outputBuf.at(0).data(), request.outputBuf.at(0).size(), ToHostMsgFlags(flags))};
        return PushBsdResultErrno(response, result);
    }

    Result IClient::RecvFrom(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        i32 flags{request.Pop<i32>()};
        if (fd == -1) {
            PushBsdResult(response, -1, BsdEbadf);
            response.Push<u32>(0);
            return {};
        }

        sockaddr_in addrIn{};
        socklen_t addrLen{sizeof(addrIn)};
        span message{request.outputBuf.at(0)};
        ssize_t result{recvfrom(fd, message.data(), message.size(), ToHostMsgFlags(flags), reinterpret_cast<sockaddr *>(&addrIn), &addrLen)};
        i32 recvErrno{errno};

        u32 guestAddrLen{};
        if (result != -1 && addrIn.sin_family == AF_INET && request.outputBuf.size() > 1 && !request.outputBuf.at(1).empty())
            guestAddrLen = HostToGuestAddress(addrIn, request.outputBuf.at(1));

        PushBsdResult(response, static_cast<i32>(result), result == -1 ? ToBsdErrno(recvErrno) : 0);
        response.Push<u32>(guestAddrLen);
        return {};
    }

    Result IClient::Send(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        i32 flags{request.Pop<i32>()};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        // MSG_NOSIGNAL prevents a write to a closed socket from raising SIGPIPE in the emulator
        ssize_t result{send(fd, request.inputBuf.at(0).data(), request.inputBuf.at(0).size(), ToHostMsgFlags(flags) | MSG_NOSIGNAL)};
        return PushBsdResultErrno(response, result);
    }

    Result IClient::SendTo(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        i32 flags{request.Pop<i32>()};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        sockaddr_in addrIn;
        if (!GuestToHostAddress(request.inputBuf.at(1), addrIn))
            return PushBsdResult(response, -1, BsdEafnosupport);

        ssize_t result{sendto(fd, request.inputBuf.at(0).data(), request.inputBuf.at(0).size(), ToHostMsgFlags(flags) | MSG_NOSIGNAL,
                              reinterpret_cast<sockaddr *>(&addrIn), sizeof(addrIn))};
        return PushBsdResultErrno(response, result);
    }

    Result IClient::Accept(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        if (fd == -1) {
            PushBsdResult(response, -1, BsdEbadf);
            response.Push<u32>(0);
            return {};
        }

        sockaddr_in addr{};
        socklen_t addrLen{sizeof(addr)};
        i32 newHostFd{accept(fd, reinterpret_cast<sockaddr *>(&addr), &addrLen)};
        i32 acceptErrno{errno};

        if (newHostFd == -1) {
            PushBsdResult(response, -1, ToBsdErrno(acceptErrno));
            response.Push<u32>(0);
            return {};
        }

        i32 guestFd{AllocateGuestFd(newHostFd)};
        if (guestFd == -1) {
            ::close(newHostFd);
            PushBsdResult(response, -1, 24); // EMFILE
            response.Push<u32>(0);
            return {};
        }

        u32 guestAddrLen{};
        if (addr.sin_family == AF_INET && !request.outputBuf.empty() && !request.outputBuf.at(0).empty())
            guestAddrLen = HostToGuestAddress(addr, request.outputBuf.at(0));

        PushBsdResult(response, guestFd, 0);
        response.Push<u32>(guestAddrLen);
        return {};
    }

    Result IClient::Bind(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        sockaddr_in addr;
        if (!GuestToHostAddress(request.inputBuf.at(0), addr))
            return PushBsdResult(response, -1, BsdEafnosupport);

        i32 result{bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))};
        return PushBsdResult(response, result, result == -1 ? ToBsdErrno(errno) : 0);
    }

    Result IClient::Connect(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        sockaddr_in addr;
        if (!GuestToHostAddress(request.inputBuf.at(0), addr))
            return PushBsdResult(response, -1, BsdEafnosupport);

        i32 result{connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))};
        return PushBsdResult(response, result, result == -1 ? ToBsdErrno(errno) : 0);
    }

    Result IClient::GetPeerName(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        if (fd == -1) {
            PushBsdResult(response, -1, BsdEbadf);
            response.Push<u32>(0);
            return {};
        }

        sockaddr_in addr{};
        socklen_t addrLen{sizeof(addr)};
        i32 result{getpeername(fd, reinterpret_cast<sockaddr *>(&addr), &addrLen)};
        i32 nameErrno{errno};

        u32 guestAddrLen{};
        if (result == 0 && addr.sin_family == AF_INET && !request.outputBuf.empty() && !request.outputBuf.at(0).empty())
            guestAddrLen = HostToGuestAddress(addr, request.outputBuf.at(0));

        PushBsdResult(response, result, result == -1 ? ToBsdErrno(nameErrno) : 0);
        response.Push<u32>(guestAddrLen);
        return {};
    }

    Result IClient::GetSockName(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        if (fd == -1) {
            PushBsdResult(response, -1, BsdEbadf);
            response.Push<u32>(0);
            return {};
        }

        sockaddr_in addr{};
        socklen_t addrLen{sizeof(addr)};
        i32 result{getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &addrLen)};
        i32 nameErrno{errno};

        u32 guestAddrLen{};
        if (result == 0 && addr.sin_family == AF_INET && !request.outputBuf.empty() && !request.outputBuf.at(0).empty())
            guestAddrLen = HostToGuestAddress(addr, request.outputBuf.at(0));

        PushBsdResult(response, result, result == -1 ? ToBsdErrno(nameErrno) : 0);
        response.Push<u32>(guestAddrLen);
        return {};
    }

    Result IClient::GetSockOpt(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        i32 level{request.Pop<i32>()};
        OptionName optionName{request.Pop<OptionName>()};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        if (level == 0xFFFF)
            level = SOL_SOCKET;

        i32 hostOption{level == SOL_SOCKET ? GetOption(optionName) : static_cast<i32>(optionName)};
        if (hostOption == -1)
            return PushBsdResult(response, 0, 0); // Options that we don't know are reported as successfully read

        socklen_t optionLen{static_cast<socklen_t>(request.outputBuf.at(0).size())};
        i32 result{getsockopt(fd, level, hostOption, request.outputBuf.at(0).data(), &optionLen)};
        return PushBsdResult(response, result, result == -1 ? ToBsdErrno(errno) : 0);
    }

    Result IClient::Listen(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        i32 backlog{request.Pop<i32>()};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        i32 result{listen(fd, backlog)};
        return PushBsdResult(response, result, result == -1 ? ToBsdErrno(errno) : 0);
    }

    Result IClient::Fcntl(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        i32 cmd{request.Pop<i32>()};
        i32 arg{request.Pop<i32>()};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        i32 result;
        if (cmd == F_GETFL) {
            // The status flags use different values on BSD (O_NONBLOCK is 0x4), games mostly care about the non-blocking flag
            result = fcntl(fd, F_GETFL);
            if (result != -1)
                result = (result & O_NONBLOCK) ? BsdOnonblock : 0;
        } else if (cmd == F_SETFL) {
            i32 flags{fcntl(fd, F_GETFL)};
            if (flags == -1) {
                result = -1;
            } else {
                flags = (arg & BsdOnonblock) ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
                result = fcntl(fd, F_SETFL, flags);
            }
        } else {
            result = fcntl(fd, cmd, arg); // F_GETFD/F_SETFD have the same values
        }

        return PushBsdResult(response, result, result == -1 ? ToBsdErrno(errno) : 0);
    }

    Result IClient::SetSockOpt(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        i32 level{request.Pop<i32>()};
        OptionName optionName{request.Pop<OptionName>()};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        if (level == 0xFFFF)
            level = SOL_SOCKET;

        i32 hostOption{level == SOL_SOCKET ? GetOption(optionName) : static_cast<i32>(optionName)};
        if (hostOption == -1)
            return PushBsdResult(response, 0, 0); // Options that we don't know are ignored instead of failing

        i32 result{setsockopt(fd, level, hostOption, request.inputBuf.at(0).data(), static_cast<socklen_t>(request.inputBuf.at(0).size()))};
        return PushBsdResult(response, result, result == -1 ? ToBsdErrno(errno) : 0);
    }

    Result IClient::Shutdown(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        i32 how{request.Pop<i32>()};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        i32 result{shutdown(fd, how)};
        return PushBsdResult(response, result, result == -1 ? ToBsdErrno(errno) : 0);
    }

    Result IClient::ShutdownAllSockets(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return PushBsdResult(response, 0, 0);
    }

    Result IClient::Write(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        i32 flags{request.Pop<i32>()};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        ssize_t result{send(fd, request.inputBuf.at(0).data(), request.inputBuf.at(0).size(), ToHostMsgFlags(flags) | MSG_NOSIGNAL)};
        return PushBsdResultErrno(response, result);
    }

    Result IClient::Read(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 fd{GetHostFd(request.Pop<i32>())};
        if (fd == -1)
            return PushBsdResult(response, -1, BsdEbadf);

        ssize_t result{recv(fd, request.outputBuf.at(0).data(), request.outputBuf.at(0).size(), 0)};
        return PushBsdResultErrno(response, result);
    }

    Result IClient::Close(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 hostFd{ReleaseGuestFd(request.Pop<i32>())};
        if (hostFd == -1)
            return PushBsdResult(response, -1, BsdEbadf); // Never close something that isn't a socket the guest created, it could be a descriptor used by the emulator

        i32 result{::close(hostFd)};
        return PushBsdResult(response, result, result == -1 ? ToBsdErrno(errno) : 0);
    }

    Result IClient::EventFd(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return PushBsdResult(response, 1, 0);
    }

    Result IClient::PushBsdResult(ipc::IpcResponse &response, i32 result, i32 errorCode) {
        if (errorCode != 0)
            result = -1;
        response.Push<i32>(result);
        response.Push<i32>(errorCode);
        return {};
    }

    Result IClient::PushBsdResultErrno(ipc::IpcResponse &response, i64 result) {
        // The result is a 32-bit value on the guest (pushing it as 64-bit shifted the errno that follows it)
        i32 hostErrno{result == -1 ? errno : 0};
        response.Push<i32>(static_cast<i32>(result));
        response.Push<i32>(result == -1 ? ToBsdErrno(hostErrno) : 0);
        return {};
    }

    i32 IClient::GetOption(OptionName optionName) {
        switch (optionName) {
            case OptionName::ReuseAddr: return SO_REUSEADDR;
            case OptionName::Broadcast: return SO_BROADCAST;
            case OptionName::Linger: return SO_LINGER;
            case OptionName::SndBuf: return SO_SNDBUF;
            case OptionName::RcvBuf: return SO_RCVBUF;
            case OptionName::SndTimeo: return SO_SNDTIMEO;
            case OptionName::RcvTimeo: return SO_RCVTIMEO;
        }

        return -1;
    }
}
