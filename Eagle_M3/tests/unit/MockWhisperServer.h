#pragma once

// Test helper: a minimal HTTP/1.1 server that emulates whisper.cpp's
// whisper-server (/inference multipart POST and GET /health). Spec §22.2.
// No Q_OBJECT: it only connects lambdas, so it needs no moc.

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <functional>

class MockWhisper : public QObject
{
public:
    struct Reply {
        int status = 200;
        QByteArray body;
        int delayMs = 0;
        bool never = false;
    };
    std::function<Reply(int index, const QByteArray &request)> responder;
    QList<QByteArray> requests;
    int open = 0;
    int maxOpen = 0;

    bool listen(quint16 port = 0)
    {
        if (!m_connected) {
            QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() { onConnection(); });
            m_connected = true;
        }
        return m_server.listen(QHostAddress::LocalHost, port);
    }
    quint16 port() const { return m_server.serverPort(); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port())); }

private:
    void onConnection()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onData(socket); });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    void onData(QTcpSocket *socket)
    {
        QByteArray &buf = m_buffers[socket];
        buf.append(socket->readAll());
        const qsizetype headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }
        qsizetype contentLength = 0;
        for (const QByteArray &line : buf.left(headerEnd).split('\n')) {
            if (line.toLower().startsWith("content-length:")) {
                contentLength = line.mid(15).trimmed().toLongLong();
            }
        }
        if (buf.size() < headerEnd + 4 + contentLength) {
            return;
        }
        const QByteArray request = buf;
        m_buffers.remove(socket);
        const int index = int(requests.size());
        requests.append(request);
        ++open;
        maxOpen = qMax(maxOpen, open);
        const Reply reply = responder ? responder(index, request) : Reply{200, "{\"text\":\"ok\"}"};
        if (reply.never) {
            QObject::connect(socket, &QTcpSocket::disconnected, this, [this]() { --open; });
            return;
        }
        QPointer<QTcpSocket> guard(socket);
        QTimer::singleShot(reply.delayMs, this, [this, guard, reply]() {
            --open;
            if (!guard) {
                return;
            }
            const QByteArray head = "HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\n"
                                    "Content-Type: application/json\r\n"
                                    "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\n"
                                    "Connection: close\r\n\r\n";
            guard->write(head + reply.body);
            guard->disconnectFromHost();
        });
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    bool m_connected = false;
};

// Value of a multipart/form-data text field in a captured request.
inline QByteArray fieldValue(const QByteArray &request, const QByteArray &name)
{
    const QByteArray marker = "name=\"" + name + "\"";
    const qsizetype at = request.indexOf(marker);
    if (at < 0) {
        return {};
    }
    const qsizetype start = request.indexOf("\r\n\r\n", at) + 4;
    const qsizetype end = request.indexOf("\r\n--", start);
    return request.mid(start, end - start);
}
