/* async_writer_source.h                                           -*- C++ -*-
   Wolfgang Sourdeau, April 2014
   Copyright (c) 2014 Datacratic.  All rights reserved.

   A base class for handling writable file descriptors.
*/

#include <sys/epoll.h>

#include <atomic>
#include <string>

#include "jml/arch/wakeup_fd.h"
#include "jml/utils/ring_buffer.h"

#include "async_event_source.h"


namespace Datacratic {

/****************************************************************************/
/* ASYNC WRITER SOURCE                                                      */
/****************************************************************************/

/* A base class enabling the asynchronous and buffered writing of data to a
 * file descriptor. This class currently implements two separate concerns (a
 * read-write "Epoller" and a write queue) and might need to be split at some
 * point. */

struct AsyncWriterSource : public AsyncEventSource
{
    /* type of callback used when a pipe or socket has been disconnected */
    typedef std::function<void(bool,
                               const std::vector<std::string> & msgs)> OnDisconnected;

    /* type of callback invoked when a string or a message has been written to
       the file descriptor */
    typedef std::function<void(int error,
                               const std::string & written,
                               size_t writtenSize)> OnWriteResult;

    /* type of callback invoked when data has been read from the file
       descriptor */
    typedef std::function<void(const char *, size_t)> OnReceivedData;

    /* type of callback invoked whenever an uncaught exception occurs */
    typedef std::function<void(const std::exception_ptr &)> OnException;

    AsyncWriterSource(const OnDisconnected & onDisconnected,
                      const OnWriteResult & onWriteResult,
                      const OnReceivedData & onReceivedData,
                      const OnException & onException,
                      /* size of the message queue */
                      size_t maxMessages,
                      /* size of the read/receive buffer */
                      size_t readBufferSize);
    virtual ~AsyncWriterSource();

    /* AsyncEventSource interface */
    virtual int selectFd() const
    { return epollFd_; }
    virtual bool processOne();

    /* enqueue "data" for writing, provided the file descriptor is open or
     * being opened, or throws */
    bool write(const std::string & data);
    bool write(const char * data, size_t size);
    bool write(std::string && data);

    /* returns whether we are ready to accept messages for sending */
    bool canSendMessages() const;

    /* invoked when a write operation has been performed, where "written" is
       the string that was sent, "writtenSize" is the amount of bytes from it
       that was sent; the latter is always equal to the length of the string
       when error is 0 */
    virtual void onWriteResult(int error,
                               const std::string & written,
                               size_t writtenSize);

    /* close the file descriptor as soon as all bytes have been sent and
     * received, implying that "write" will never be invoked anymore */
    void requestClose();

    /* invoked when the connection is closed */
    virtual void onDisconnected(bool fromPeer,
                                const std::vector<std::string> & msgs);

    /* invoked when the data is available for reading */
    virtual void onReceivedData(const char * data, size_t size);

    /* invoked when an exception occurs during the handling of events */
    virtual void onException(const std::exception_ptr & excPtr);

    /* number of bytes actually sent */
    uint64_t bytesSent() const
    { return bytesSent_; }

    uint64_t bytesReceived() const
    { return bytesReceived_; }

    /* number of messages actually sent */
    size_t msgsSent() const
    { return msgsSent_; }

protected:
    /* set the "main" file descriptor, for which epoll events are monitored
     * and the onWriteResult, onReceivedData and onDisconnected callbacks are
     * invoked automatically */
    void setFd(int fd);
    int getFd()
        const
    {
        return fd_;
    }

    /* close the "main" file descriptor and take care of the surrounding
       operations */
    virtual void closeFd();

    /* type of callback invoked whenever an epoll event is reported for a
     * file descriptor */
    typedef std::function<void (const ::epoll_event &)> EpollCallback;

    /* register a file descriptor into the internal epoll queue for reading
       and/or writing */
    void addFd(int fd, bool readerFd, bool writerFd)
    { performAddFd(fd, readerFd, writerFd, false, false); }

    /* same as addFd, with the EPOLLONESHOT flag */
    void addFdOneShot(int fd, bool readerFd, bool writerFd)
    { performAddFd(fd, readerFd, writerFd, false, true); }

    /* modify a file descriptor in the epoll queue */
    void modifyFd(int fd, bool readerFd, bool writerFd)
    { performAddFd(fd, readerFd, writerFd, true, false); }

    /* same as modifyFd, with the EPOLLONESHOT flag */
    void modifyFdOneShot(int fd, bool readerFd, bool writerFd)
    { performAddFd(fd, readerFd, writerFd, true, true); }

    /* remove a file descriptor from the internal epoll queue */
    void removeFd(int fd);

    /* associate a callback with a file descriptor for future epoll
       operations */
    void registerFdCallback(int fd, const EpollCallback & cb);

    /* disassociate a callback and a file descriptor from the callback
       registry */
    void unregisterFdCallback(int fd);

private:
    void performAddFd(int fd, bool readerFd, bool writerFd,
                      bool modify, bool oneshot);

    /* epoll operations */
    void closeEpollFd();

    /* fd operations */
    void flush();

    void handleFdEvent(const ::epoll_event & event);
    void handleReadReady();
    void handleWriteReady();
    void handleWriteResult(int error,
                           const std::string & written, size_t writtenSize);
    void handleDisconnection(bool fromPeer);
    void handleException();

    /* wakeup operations */
    void handleWakeupEvent(const ::epoll_event & event);

    int epollFd_;
    size_t numFds_;

    std::map<int, EpollCallback> fdCallbacks_;

    int fd_;
    bool closing_;
    size_t readBufferSize_;
    bool writeReady_;

    ML::RingBufferSRMW<std::string> threadBuffer_;
    std::atomic<size_t> remainingMsgs_;
    std::string currentLine_;
    size_t currentSent_;

    uint64_t bytesSent_;
    uint64_t bytesReceived_;
    size_t msgsSent_;

    ML::Wakeup_Fd wakeup_;

    OnDisconnected onDisconnected_;
    OnWriteResult onWriteResult_;
    OnReceivedData onReceivedData_;
    OnException onException_;
};

}
