// Copyright (c) 2012 Plenluno All rights reserved.

#ifndef LIBNODE_SRC_HTTP_PARSER_H_
#define LIBNODE_SRC_HTTP_PARSER_H_

#include <assert.h>
#include <http_parser.h>
#include <libj/string.h>

#include "libnode/buffer.h"
#include "./incoming_message.h"
#include "../flag.h"

namespace libj {
namespace node {
namespace http {

class Parser {
 public:
    Parser(enum http_parser_type type)
        : flags_(0)
        , url_(String::null())
        , method_(String::null())
        , maxHeaderPairs_(0)
        , fields_(JsArray::create())
        , values_(JsArray::create()) {
        http_parser_init(&parser_, type);
        parser_.data = this;
    }

    Int execute(Buffer::CPtr buf) {
        static Boolean initSettings = false;
        if (!initSettings) {
            settings.on_message_begin = Parser::onMessageBegin;
            settings.on_url = Parser::onUrl;
            settings.on_header_field = Parser::onHeaderField;
            settings.on_header_value = Parser::onHeaderValue;
            settings.on_headers_complete = Parser::onHeadersComplete;
            settings.on_body = Parser::onBody;
            settings.on_message_complete = Parser::onMessageComplete;
        }

        size_t len = buf->length();
        size_t numParsed = http_parser_execute(
                                &parser_,
                                &settings,
                                static_cast<const char*>(buf->data()),
                                len);
        if (!parser_.upgrade && numParsed != len) {
            return -1;
        } else {
            return numParsed;
        }
    }

    Boolean finish() {
        size_t numParsed = http_parser_execute(
                                &parser_,
                                &settings,
                                NULL,
                                0);
        return !numParsed;
    }

 private:
    #define LIBNODE_STR_UPDATE(STR, AT, LEN) \
        if (!STR) { \
            STR = String::create(AT, String::UTF8, LEN); \
        } else { \
            STR = STR->concat(String::create(AT, String::UTF8, LEN)); \
        }

    static int onMessageBegin(http_parser* parser) {
        Parser* self = static_cast<Parser*>(parser->data);
        self->url_ = String::null();
        self->fields_->clear();
        self->values_->clear();
        return 0;
    }

    static int onUrl(http_parser* parser, const char* at, size_t len) {
        Parser* self = static_cast<Parser*>(parser->data);
        LIBNODE_STR_UPDATE(self->url_, at, len);
        return 0;
    }

    static int onHeaderField(
        http_parser* parser, const char* at, size_t len) {
        Parser* self = static_cast<Parser*>(parser->data);
        JsArray::Ptr fields = self->fields_;
        JsArray::Ptr values = self->values_;
        Size numFields = fields->size();
        Size numValues = values->size();
        if (numFields == numValues) {
            fields->add(String::null());
        }

        assert(fields->size() == numValues + 1);
        LIBNODE_STR_UPDATE(fields->getCPtr<String>(numValues), at, len);
        return 0;
    }

    static int onHeaderValue(
        http_parser* parser, const char* at, size_t len) {
        Parser* self = static_cast<Parser*>(parser->data);
        JsArray::Ptr fields = self->fields_;
        JsArray::Ptr values = self->values_;
        Size numFields = fields->size();
        Size numValues = values->size();
        if (numValues != numFields) {
            values->add(String::null());
        }

        assert(values->size() == numFields);
        LIBNODE_STR_UPDATE(values->getCPtr<String>(numFields - 1), at, len);
        return 0;
    }

    static int onHeadersComplete(http_parser* parser) {
        static const String::CPtr methodDelete = String::intern("DELETE");
        static const String::CPtr methodGet = String::intern("GET");
        static const String::CPtr methodHead = String::intern("HEAD");
        static const String::CPtr methodPost = String::intern("POST");
        static const String::CPtr methodPut = String::intern("PUT");
        static const String::CPtr methodConnect = String::intern("CONNECT");
        static const String::CPtr methodOptions = String::intern("OPTIONS");
        static const String::CPtr methodTrace = String::intern("TRACE");

        Parser* self = static_cast<Parser*>(parser->data);
        if (parser->type == HTTP_REQUEST) {
            String::CPtr method;
            switch (parser->method) {
            case HTTP_DELETE:
                method = methodDelete;
                break;
            case HTTP_GET:
                method = methodGet;
                break;
            case HTTP_HEAD:
                method = methodHead;
                break;
            case HTTP_POST:
                method = methodPost;
                break;
            case HTTP_PUT:
                method = methodPut;
                break;
            case HTTP_CONNECT:
                method = methodConnect;
                break;
            case HTTP_OPTIONS:
                method = methodOptions;
                break;
            case HTTP_TRACE:
                method = methodTrace;
                break;
            default:
                method = String::create();
            }
            self->method_ = method;
        } else if (parser->type == HTTP_RESPONSE) {
            self->statusCode_ = static_cast<Int>(parser->status_code);
        }

        self->majorVer_ = static_cast<Int>(parser->http_major);
        self->minorVer_ = static_cast<Int>(parser->http_minor);
        if (parser->upgrade) {
            self->setFlag(UPGRADE);
        }
        if (http_should_keep_alive(parser)) {
            self->setFlag(SHOULD_KEEP_ALIVE);
        }

        return self->onHeadersComplete() ? 1 : 0;
    }

    static int onBody(http_parser* parser, const char* at, size_t length) {
        Parser* self = static_cast<Parser*>(parser->data);
        self->onBody(Buffer::create(at, length));
        return 0;
    }

    static int onMessageComplete(http_parser* parser) {
        Parser* self = static_cast<Parser*>(parser->data);
        self->onMessageComplete();
        return 0;
    }

 private:
    int onHeadersComplete() {
        StringBuffer::Ptr httpVer = StringBuffer::create();
        httpVer->append(majorVer_);
        httpVer->appendChar('.');
        httpVer->append(minorVer_);

        incoming_ = IncomingMessage::create(socket_);
        incoming_->setUrl(url_);
        incoming_->setHttpVersion(httpVer->toString());

        Size n = fields_->length();
        assert(values_->length() == n);
        if (!maxHeaderPairs_) {
            n = n < maxHeaderPairs_ ? n : maxHeaderPairs_;
        }
        for (Size i = 0; i < n; i++) {
            incoming_->addHeaderLine(
                fields_->getCPtr<String>(i),
                values_->getCPtr<String>(i));
        }

        url_ = String::null();
        fields_ = JsArray::create();
        values_ = JsArray::create();

        if (method_) {
            incoming_->setMethod(method_);
        } else {
            incoming_->setStatusCode(statusCode_);
        }

        if (hasFlag(UPGRADE))
            incoming_->setFlag(IncomingMessage::UPGRADE);

        Boolean skipBody = false;
        if (!hasFlag(UPGRADE)) {
            Value r = onIncoming_->call(incoming_, hasFlag(SHOULD_KEEP_ALIVE));
            to<Boolean>(r, &skipBody);
        }
        return skipBody;
    }

    void onBody(Buffer::CPtr buf) {
        LinkedList::Ptr pendings = incoming_->getPendings();
        if (incoming_->hasFlag(IncomingMessage::PAUSED) ||
            pendings->length()) {
            pendings->push(buf);
        } else {
            incoming_->emitData(buf);
        }
    }

    void onMessageComplete() {
        incoming_->setFlag(IncomingMessage::COMPLETE);

        if (!fields_->isEmpty()) {
            Size n = fields_->size();
            assert(values_->size() == n);
            for (Size i = 0; i < n; i++) {
                incoming_->addHeaderLine(
                    fields_->getCPtr<String>(i),
                    values_->getCPtr<String>(i));
            }
            url_ = String::null();
            fields_->clear();
            values_->clear();
        }

        if (!incoming_->hasFlag(IncomingMessage::UPGRADE)) {
            LinkedList::Ptr pendings = incoming_->getPendings();
            if (incoming_->hasFlag(IncomingMessage::PAUSED) ||
                pendings->length()) {
                pendings->push(0);  // EOF
            } else {
                incoming_->unsetFlag(IncomingMessage::READABLE);
                incoming_->emitEnd();
            }
        }

        if (socket_->readable()) {
            // socket_->resume();
        }
    }

 private:
    enum Flag {
        HAVE_FLUSHED      = 1 << 0,
        UPGRADE           = 1 << 1,
        SHOULD_KEEP_ALIVE = 1 << 2,
    };

    LIBNODE_FLAG_METHODS(Flag, flags_);

 private:
    static http_parser_settings settings;

 private:
    UInt flags_;
    http_parser parser_;
    String::CPtr url_;
    String::CPtr method_;
    Int majorVer_;
    Int minorVer_;
    Int statusCode_;
    Size maxHeaderPairs_;
    JsArray::Ptr fields_;
    JsArray::Ptr values_;
    net::SocketImpl::Ptr socket_;
    IncomingMessage::Ptr incoming_;
    JsFunction::Ptr onIncoming_;

    #undef LIBNODE_STR_UPDATE
};

}  // namespace http
}  // namespace node
}  // namespace libj

#endif  // LIBNODE_SRC_HTTP_PARSER_H_
