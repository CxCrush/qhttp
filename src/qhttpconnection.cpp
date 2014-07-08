/*
 * Copyright 2011-2014 Nikhil Marathe <nsm.nikhil@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
///////////////////////////////////////////////////////////////////////////////

#include "private/qhttpconnection_private.hpp"
///////////////////////////////////////////////////////////////////////////////
namespace qhttp {
namespace server {
///////////////////////////////////////////////////////////////////////////////
QHttpConnection::QHttpConnection(QObject *parent)
    : QTcpSocket(parent), d_ptr(new QHttpConnectionPrivate(this)) {
    QHTTP_LINE_LOG
}

QHttpConnection::QHttpConnection(QHttpConnectionPrivate& dd, QObject* parent)
    : QTcpSocket(parent), d_ptr(&dd) {
    QHTTP_LINE_LOG
}

QHttpConnection::~QHttpConnection() {
    QHTTP_LINE_LOG
}

void
QHttpConnection::setTimeOut(quint32 miliSeconds) {
    if ( miliSeconds != 0 )
        d_func()->itimer.start(miliSeconds, this);
}

void
QHttpConnection::killConnection() {
    disconnectFromHost();
}

QHttpRequest*
QHttpConnection::latestRequest() const {
    return d_func()->irequest;
}

QHttpResponse*
QHttpConnection::latestResponse() const {
    return d_func()->iresponse;
}

void
QHttpConnection::timerEvent(QTimerEvent *) {
    disconnectFromHost();
}

///////////////////////////////////////////////////////////////////////////////

int
QHttpConnectionPrivate::messageBegin(http_parser*) {
    itempUrl.clear();
    itempUrl.reserve(128);

    QTcpSocket* sok = static_cast<QTcpSocket*>(q_ptr);
    Q_ASSERT(sok);
    irequest = new QHttpRequest(sok);
    return 0;
}

int
QHttpConnectionPrivate::url(http_parser*, const char* at, size_t length) {
    Q_ASSERT(irequest);

    itempUrl.append(at, length);
    return 0;
}

int
QHttpConnectionPrivate::headerField(http_parser*, const char* at, size_t length) {
    Q_ASSERT(irequest);

    // insert the header we parsed previously
    // into the header map
    if ( !itempHeaderField.isEmpty() && !itempHeaderValue.isEmpty() ) {
        // header names are always lower-cased
        irequest->d_func()->iheaders.insert(
                    itempHeaderField.toLower(),
                    itempHeaderValue.toLower()
                    );
        // clear header value. this sets up a nice
        // feedback loop where the next time
        // HeaderValue is called, it can simply append
        itempHeaderField.clear();
        itempHeaderValue.clear();
    }

    itempHeaderField.append(at, length);
    return 0;
}

int
QHttpConnectionPrivate::headerValue(http_parser*, const char* at, size_t length) {
    Q_ASSERT(irequest);

    itempHeaderValue.append(at, length);
    return 0;
}

int
QHttpConnectionPrivate::headersComplete(http_parser* parser) {
    Q_ASSERT(irequest);

#if defined(USE_CUSTOM_URL_CREATOR)
    // get parsed url
    struct http_parser_url urlInfo;
    int r = http_parser_parse_url(itempUrl.constData(),
                                  itempUrl.size(),
                                  parser->method == HTTP_CONNECT,
                                  &urlInfo);
    Q_ASSERT(r == 0);
    Q_UNUSED(r);

    irequest->d_func()->iurl = createUrl(
                                 itempUrl.constData(),
                                 urlInfo
                                 );
#else
    irequest->d_func()->iurl = QUrl(itempUrl);
#endif // defined(USE_CUSTOM_URL_CREATOR)

    // set method
    irequest->d_func()->imethod =
            static_cast<THttpMethod>(parser->method);

    // set version
    irequest->d_func()->iversion = QString("%1.%2")
                                 .arg(parser->http_major)
                                 .arg(parser->http_minor);

    // Insert last remaining header
    irequest->d_func()->iheaders.insert(
                itempHeaderField.toLower(),
                itempHeaderValue.toLower()
                );

    // set client information
    irequest->d_func()->iremoteAddress = isocket->peerAddress().toString();
    irequest->d_func()->iremotePort    = isocket->peerPort();



    iresponse = new QHttpResponse(isocket);

    if ( parser->http_major < 1 || parser->http_minor < 1  ) {

        iresponse->d_func()->ikeepAlive = false;
    }

    // we are good to go!
    emit q_ptr->newRequest(irequest, iresponse);
    return 0;
}

int
QHttpConnectionPrivate::body(http_parser*, const char* at, size_t length) {
    Q_ASSERT(irequest);

    emit irequest->data(QByteArray(at, length));
    return 0;
}

int
QHttpConnectionPrivate::messageComplete(http_parser*) {
    Q_ASSERT(irequest);

    irequest->d_func()->isuccessful = true;
    emit irequest->end();
    return 0;
}



///////////////////////////////////////////////////////////////////////////////
#if defined(USE_CUSTOM_URL_CREATOR)
///////////////////////////////////////////////////////////////////////////////
/* URL Utilities */
#define HAS_URL_FIELD(info, field) (info.field_set &(1 << (field)))

#define GET_FIELD(data, info, field)                                                               \
    QString::fromLatin1(data + info.field_data[field].off, info.field_data[field].len)

#define CHECK_AND_GET_FIELD(data, info, field)                                                     \
    (HAS_URL_FIELD(info, field) ? GET_FIELD(data, info, field) : QString())

QUrl
QHttpConnectionPrivate::createUrl(const char *urlData, const http_parser_url &urlInfo) {
    QUrl url;
    url.setScheme(CHECK_AND_GET_FIELD(urlData, urlInfo, UF_SCHEMA));
    url.setHost(CHECK_AND_GET_FIELD(urlData, urlInfo, UF_HOST));
    // Port is dealt with separately since it is available as an integer.
    url.setPath(CHECK_AND_GET_FIELD(urlData, urlInfo, UF_PATH));
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    url.setQuery(CHECK_AND_GET_FIELD(urlData, urlInfo, UF_QUERY));
#else
    if (HAS_URL_FIELD(urlInfo, UF_QUERY)) {
        url.setEncodedQuery(QByteArray(urlData + urlInfo.field_data[UF_QUERY].off,
                                       urlInfo.field_data[UF_QUERY].len));
    }
#endif
    url.setFragment(CHECK_AND_GET_FIELD(urlData, urlInfo, UF_FRAGMENT));
    url.setUserInfo(CHECK_AND_GET_FIELD(urlData, urlInfo, UF_USERINFO));

    if (HAS_URL_FIELD(urlInfo, UF_PORT))
        url.setPort(urlInfo.port);

    return url;
}

#undef CHECK_AND_SET_FIELD
#undef GET_FIELD
#undef HAS_URL_FIELD
///////////////////////////////////////////////////////////////////////////////
#endif // defined(USE_CUSTOM_URL_CREATOR)

///////////////////////////////////////////////////////////////////////////////
} // namespace server
} // namespace qhttp
///////////////////////////////////////////////////////////////////////////////
