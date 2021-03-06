/**
 * AdvertCast
 *
 * Piotr Rżysko
 * 27.12.2015
 */

#include "UDPServer.hpp"

bool UDPLoopGuard = true;
unsigned int UDPSleep = 5;

UDPServer::UDPServer() { }

UDPServer::UDPServer(std::string multicastAddr, std::string multicastInterface, std::string port, ServerController *parent)
        : parent(parent), lastFileId(0)
{
    if (initServer(multicastAddr, multicastInterface, port))
    {
        logger::info << "Serwer UDP zostal uruchomiony.\n";
    } else
    {
        logger::error << "Wystapil blad podczas uruchamiania serwera UDP.\n";
    }
}

void UDPServer::start()
{
    sleep(UDPSleep);
    while(UDPLoopGuard)
    {
        VideoFile videofileToSend = getFromQueue();

        std::ifstream videoFile;
        videoFile.open(videofileToSend.getLocalPath());
        bool isBegin = true;
        uint datagramNumber = 1;
        while(!videoFile.eof())
        {
            int maxDataSize = MAX_DATAGRAM_SIZE - DATAGRAM_CUSTOM_HEADER_SIZE;
            char bytesFromFile[maxDataSize];
            memset(bytesFromFile, 0, maxDataSize);
            videoFile.read(bytesFromFile, maxDataSize);

            std::string bytesStr(bytesFromFile, videoFile.gcount());
            if (isBegin)
            {
                sendDatagram(datagramNumber, videofileToSend.getId(), UdpMessagesTypes::Begin, bytesStr,videofileToSend.getTimestamp());
            } else
            {
                sendDatagram(datagramNumber, videofileToSend.getId(), UdpMessagesTypes::Middle, bytesStr,videofileToSend.getTimestamp());
            }
            isBegin = false;
            datagramNumber++;
        }
        sendDatagram(datagramNumber, videofileToSend.getId(), UdpMessagesTypes::End, "", videofileToSend.getTimestamp());
        videoFile.close();
    }
}

void UDPServer::addFiles(std::list<VideoFile> filesToSend)
{
    for(std::list<VideoFile>::iterator iter = filesToSend.begin(); iter != filesToSend.end(); iter++)
    {
        lastFileId++;
        videoFiles[lastFileId] = *iter;
        addFileToQueue(lastFileId);
    }
}

void UDPServer::addFileToQueue(uint fileId)
{
    std::unique_lock<std::mutex> mlock(mutex);
    if (videoFiles.find(fileId) != videoFiles.end()
        && videoFilesInQueue.find(fileId) == videoFilesInQueue.end())
    {
        VideoFile videoToQueue = videoFiles[fileId];
        videoToQueue.setId(fileId);
        filesToSendQueue.push(videoToQueue);
        videoFilesInQueue[fileId] = TransmisionType::MULTICAST;
        logger::info << "Added to queue file_id = [" << fileId << "].\n";
        mlock.unlock();
        cond.notify_one();
    } else
    {
        mlock.unlock();
    }
}

VideoFile UDPServer::getFromQueue()
{
    std::unique_lock<std::mutex> mlock(mutex);
    while (filesToSendQueue.empty())
    {
        cond.wait(mlock);
    }
    VideoFile item = filesToSendQueue.top();
    filesToSendQueue.pop();
    videoFilesInQueue.erase(item.getId());
    mlock.unlock();
    return item;
}

bool UDPServer::initServer(std::string multicastAddr, std::string multicastInterface, std::string port)
{
    SocketFactory socketFactory;
    MulticastUtils multicastUtils;

    memset(&addr, 0, sizeof(addr));
    sockfd = socketFactory.createSocket(multicastAddr, port, AF_UNSPEC, SOCK_DGRAM, &addr, false);
    if (sockfd == -1)
    {
        logger::error << "Creating socket error.\n";
        return false;
    }

    if (!multicastUtils.isMulticastAddress(&addr))
    {
        logger::error << "Wrong multicast address.\n";
        close(sockfd);
        return false;
    }

    if (!multicastUtils.setMulticastInterface(sockfd, multicastInterface, &addr))
    {
        logger::error << "Setting local interface error.\n";
        close(sockfd);
        return false;
    }
    return true;
}

bool UDPServer::sendDatagram(uint datagramNumber, uint fileId, std::string type, std::string data, std::time_t timestamp)
{
    char datagram[MAX_DATAGRAM_SIZE];
    std::stringstream ss("");

    ss << type << " " << fileId << " " << datagramNumber << " " << timestamp << " " << data.size() << "\n" << data;
    memset(datagram, 0, MAX_DATAGRAM_SIZE);
    memcpy(datagram, ss.str().c_str(), ss.str().size());
    if (sendto(sockfd, datagram, MAX_DATAGRAM_SIZE, 0, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        logger::error << "Error in sendto function.\n";
        return false;
    }
    return true;
}
