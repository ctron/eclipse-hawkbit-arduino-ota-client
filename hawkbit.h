/*******************************************************************************
 * Copyright (c) 2020 Red Hat Inc
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0
 *
 * SPDX-License-Identifier: EPL-2.0
 *******************************************************************************/

#ifndef _HAWBIT_H_
#define _HAWBIT_H_

#include <vector>
#include <utility>
#include <WString.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <map>
#include <list>

class Artifact;
class Chunk;
class Deployment;
class State;
class UpdateResult;
class DownloadResult;
class HawkbitClient;

class UpdateResult {
    public:
        UpdateResult(uint32_t code) :
            _code(code)
        {
        }

        uint32_t code() const { return this->_code; }

    private:
        uint32_t _code;
};

class DownloadResult {
    public:
        DownloadResult(uint32_t code) :
            _code(code)
        {
        }

        uint32_t code() const { return this->_code; }

    private:
        uint32_t _code;
};

class Artifact {
    public:
        Artifact(
            const String& filename,
            uint32_t size,
            const std::map<String,String>& hashes,
            const std::map<String,String>& links
            ) :
            _filename(filename),
            _size(size),
            _hashes(hashes),
            _links(links)
        {
        }

        const String& filename() const { return _filename; }
        const uint32_t size() const { return _size; }
        const std::map<String,String>& hashes() const { return _hashes; }
        const std::map<String,String>& links() const { return _links; }

        void dump(Print& out, const String& prefix = "") const {
            out.printf("%s%s %u\n", prefix.c_str(), this->_filename.c_str(), this->_size);
            out.printf("%sHashes\n", prefix.c_str());
            for (std::pair<String,String> element : this->_hashes) {
                out.printf("%s    %s = %s\n", prefix.c_str(), element.first.c_str(), element.second.c_str());
            }
            out.printf("%sLinks\n", prefix.c_str());
            for (std::pair<String,String> element : this->_links) {
                out.printf("%s    %s = %s\n", prefix.c_str(), element.first.c_str(), element.second.c_str());
            }
        }

    private:
        String _filename;
        uint32_t _size;
        std::map<String,String> _hashes;
        std::map<String,String> _links;
};

class Chunk {
    public:
        Chunk(const String& part, const String& version, const String& name, const std::list<Artifact>& artifacts) :
            _part(part),
            _version(version),
            _name(name),
            _artifacts(artifacts)
        {
        }

        const String& part() const { return _part; }
        const String& version() const { return _version; }
        const String& name() const { return _name; }
        const std::list<Artifact>& artifacts() const { return _artifacts; }

        void dump(Print& out, const String& prefix = "") const {
            out.printf("%s%s - %s (%s)\n", prefix.c_str(), this->_name.c_str(), this->_version.c_str(), this->_part.c_str());
            for (Artifact a: this->_artifacts) {
                a.dump(out, prefix + "    ");
            }
        }

    private:
        String _part;
        String _version;
        String _name;
        std::list<Artifact> _artifacts;
};

class Deployment {
    public:
        Deployment() {
        }

        Deployment(const String& id, const String& download, const String& update, const std::list<Chunk>& chunks) :
            _id(id),
            _download(download),
            _update(update),
            _chunks(chunks)
        {
        }

        const String& id() const { return _id; }
        const std::list<Chunk>& chunks() const { return _chunks; }

        void dump(Print& out, const String& prefix = "") const {
            out.printf("%sDeployment: %s\n", prefix.c_str(), this->_id.c_str());
            out.printf("%s    Download: %s, Update: %s\n", prefix.c_str(), this->_download.c_str(), this->_update.c_str());
            out.printf("%s    Chunks:\n", prefix.c_str());
            String chunkPrefix = prefix + "        ";
            for (Chunk c : this->_chunks) {
                c.dump(out, chunkPrefix);
            }
            out.println();
        };
    private:
        String _id;
        String _download;
        String _update;
        std::list<Chunk> _chunks;
};

class Stop {
    public:
        Stop() {
        }

        Stop(const String&id) :
            _id(id)
        {}

        const String& id() const { return this->_id; }

        void dump(Print& out, const String& prefix = "") const
        {
            out.printf("%sStop: %s\n", prefix.c_str(), this->_id.c_str());
        }
    private:
        String _id;
};

class Registration {
    public:
        Registration()
        {
        }

        Registration(const String& url):
            _url(url)
        {
        }

        const String& url() const { return this->_url; }

        void dump(Print& out, const String& prefix = "") const
        {
            out.printf("%sRegistration: %s\n", prefix.c_str(), this->_url.c_str());
        }

    private:
        String _url;
};

class State {

    public:

        typedef enum { NONE, REGISTER, UPDATE, CANCEL } Type;

        State() :
            _type(State::NONE)
        {
        }

        State(const Stop& stop) :
            _type(State::CANCEL),
            _stop(stop)
        {
        }

        State(const Registration& registration) :
            _type(State::REGISTER),
            _registration(registration)
        {
        }

        State(const Deployment& deployment) :
            _type(State::UPDATE),
            _deployment(deployment)
        {
        }

        boolean is(Type type) const
        {
            return this->_type == type;
        }

        const Type type() const { return this->_type; }
        const Deployment& deployment() const { return this->_deployment; }
        const Stop& stop() const { return this->_stop; }
        const Registration& registration() const { return this->_registration; }

        void dump(Print& out, const String& prefix = "") const
        {
            switch (this->_type) {
                case State::NONE:
                    out.printf("%sState <NONE>\n", prefix.c_str());
                    break;
                case State::UPDATE:
                    out.printf("%sState <UPDATE>\n", prefix.c_str());
                    this->_deployment.dump(out, "    ");
                    break;
                case State::CANCEL:
                    out.printf("%sState <CANCEL>\n", prefix.c_str());
                    this->_stop.dump(out, "    ");
                    break;
                case State::REGISTER:
                    out.printf("%sState <REGISTER>\n", prefix.c_str());
                    this->_registration.dump(out, "    ");
                    break;
                default:
                    out.printf("%sState <UNKNOWN>\n", prefix.c_str());
                    break;
            }
        }

    private:
        Type _type;
        Deployment _deployment;
        Stop _stop;
        Registration _registration;
};

class DownloadError {
    public:
        DownloadError(uint32_t code) :
            _code(code)
        {
        }

        uint32_t code() const { return this->_code; }

    private:
        uint32_t _code;
};

class Download {
    public:
        Stream& stream() { return this->_stream; }

    private:
        Stream& _stream;

        Download(Stream& stream) :
            _stream(stream)
         {
         }

    friend HawkbitClient;
};

class HawkbitClient {
    public:

        typedef enum { MERGE, REPLACE, REMOVE } MergeMode;

        HawkbitClient(
            WiFiClient &wifi,
            const String& baseUrl,
            const String& tenantName,
            const String& controllerId,
            const String& securityToken);

        State readState();

        template<typename DownloadHandler>
        void download(const Artifact& artifact, DownloadHandler function, const String& linkType = "download")
        {
            auto href = artifact.links().find(linkType);

            if ( href == artifact.links().end()) {
                throw String("Missing link for download");
            }

            _http.begin(this->_wifi, href->second);

            _http.addHeader("Authorization", this->_authToken);

            int code = _http.GET();
            log_i("Result - code: %d", code);

            if (code == HTTP_CODE_OK ) {
                Download d(_http.getStream());
                function(d);
            }

            _http.end();

            if (code != HTTP_CODE_OK ) {
                throw DownloadError(code);
            }
        };

        UpdateResult reportProgress(const Deployment& deployment, uint32_t done, uint32_t total, std::vector<String> details = {});

        UpdateResult reportComplete(const Deployment& deployment, bool success = true, std::vector<String> details = {});
        
        UpdateResult reportScheduled(const Deployment& deployment, std::vector<String> details = {});
        
        UpdateResult reportResumed(const Deployment& deployment, std::vector<String> details = {});
        
        UpdateResult reportCancelAccepted(const Stop& stop, std::vector<String> details = {});
        
        UpdateResult reportCancelRejected(const Stop& stop, std::vector<String> details = {});

        UpdateResult updateRegistration(const Registration& registration, const std::map<String,String>& data, MergeMode mergeMode = REPLACE, std::initializer_list<String> details = {});

    private:
        WiFiClient&_wifi;
    
        HTTPClient _http;

        String _baseUrl;
        String _tenantName;
        String _controllerId;
        String _authToken;

        Deployment readDeployment(const String& href);
        Stop readCancel(const String& href);

        String feedbackUrl(const Deployment& deployment) const;
        String feedbackUrl(const Stop& stop) const;

        template<typename IdProvider>
        UpdateResult sendFeedback(IdProvider id, const String& execution, const String& finished, std::vector<String> details );
};

#endif // _HAWBIT_H_
