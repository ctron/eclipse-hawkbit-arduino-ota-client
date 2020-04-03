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

#include "hawkbit.h"

#include <Arduino.h>

HawkbitClient::HawkbitClient(
    JsonDocument& doc,
    WiFiClient& wifi,
    const String& baseUrl,
    const String& tenantName,
    const String& controllerId,
    const String &securityToken) :
    _doc(doc),
    _wifi(wifi),
    _baseUrl(baseUrl),
    _tenantName(tenantName),
    _controllerId(controllerId),
    _authToken("TargetToken " + securityToken)
{
}

UpdateResult HawkbitClient::updateRegistration(const Registration& registration, const std::map<String,String>& data, MergeMode mergeMode, std::initializer_list<String> details)
{
    _doc.clear();

    switch(mergeMode) {
        case MERGE:
            _doc["mode"] = "merge";
            break;
        case REPLACE:
            _doc["mode"] = "replace";
            break;
        case REMOVE:
            _doc["mode"] = "remove";
            break;
    }

    _doc.createNestedObject("data");
    _doc["data"]["mac"] = WiFi.macAddress();
    for (const std::pair<String,String>& entry : data) {
        _doc["data"][String(entry.first)] = entry.second;
    }

    JsonArray d = _doc["status"].createNestedArray("details");
    for (auto detail : details) {
        d.add(detail);
    }

    _doc["status"]["execution"] = "closed";
    _doc["status"]["result"]["finished"] = "success";

    _http.begin(this->_wifi, registration.url());

    _http.addHeader("Accept", "application/hal+json");
    _http.addHeader("Content-Type", "application/json");
    _http.addHeader("Authorization", this->_authToken);

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
    serializeJsonPretty(_doc, Serial);
#endif

    String buffer;
    size_t len = serializeJson(_doc, buffer);
    (void)len; // ignore unused

    log_d("JSON - len: %d", len);

    int code = _http.PUT(buffer);
    log_d("Result - code: %d", code);

    String resultPayload = _http.getString();
    log_d("Result - payload: %s", resultPayload.c_str());

    _http.end();

    return UpdateResult(code);
}

State HawkbitClient::readState()
{
    _http.begin(this->_wifi, this->_baseUrl + "/" + this->_tenantName + "/controller/v1/" + this->_controllerId);

    _http.addHeader("Authorization", this->_authToken);
    _http.addHeader("Accept", "application/hal+json");

    _doc.clear();

    int code = _http.GET();
    log_d("Result - code: %d", code);
    String resultPayload = _http.getString();
    log_d("Result - payload: %s", resultPayload.c_str());
    if ( code == HTTP_CODE_OK ) {
        DeserializationError error = deserializeJson(_doc, resultPayload);
        if (error) {
            _http.end();
            // FIXME: need a way to handle errors
            throw 1;
        }
    }
    _http.end();

    String href = _doc["_links"]["deploymentBase"]["href"] | "";
    if (!href.isEmpty()) {
        log_d("Fetching deployment: %s", href.c_str());
        return State(this->readDeployment(href));
    }

    href = _doc["_links"]["configData"]["href"] | "";
    if (!href.isEmpty()) {
        log_d("Need to register", href.c_str());
        return State(Registration(href));
    }

    href = _doc["_links"]["cancelAction"]["href"] | "";
    if (!href.isEmpty()) {
        log_d("Fetching cancel action: %s", href.c_str());
        return State(this->readCancel(href));
    }

    log_d("No update");
    return State();
}

std::map<String,String> toMap(const JsonObject& obj) {
    std::map<String,String> result;
    for (const JsonPair& p: obj) {
        if (p.value().is<char*>()) {
            result[String(p.key().c_str())] = String(p.value().as<char*>());
        }
    }
    return result;
}

std::map<String,String> toLinks(const JsonObject& obj) {
    std::map<String,String> result;
    for (const JsonPair& p: obj) {
        const char* key = p.key().c_str();
        const char* value = p.value()["href"];
        result[String(key)] = String(value);
    }
    return result;
}

std::list<Artifact> artifacts(const JsonArray& artifacts)
{
    std::list<Artifact> result;

    for (JsonObject o : artifacts) {
        Artifact artifact (
            o["filename"],
            o["size"] | 0,
            toMap(o["hashes"]),
            toLinks(o["_links"])
        );
        result.push_back(artifact);
    }

    return result;
}

std::list<Chunk> chunks(const JsonArray& chunks)
{
    std::list<Chunk> result;

    for(JsonObject o : chunks)
    {
        Chunk chunk(
            o["part"],
            o["version"],
            o["name"],
            artifacts(o["artifacts"])
            );
        result.push_back(chunk);
    }

    return result;
}

Deployment HawkbitClient::readDeployment(const String& href)
{
    _http.begin(this->_wifi, href);

    _http.addHeader("Authorization", this->_authToken);
    _http.addHeader("Accept", "application/hal+json");

    _doc.clear();

    int code = _http.GET();
    log_d("Result - code: %d", code);
    String resultPayload = _http.getString();
    log_d("Result - payload: %s", resultPayload.c_str());
    if ( code == HTTP_CODE_OK ) {
        DeserializationError error = deserializeJson(_doc, resultPayload);
        if (error) {
            _http.end();
            // FIXME: need a way to handle errors
            throw 1;
        }
    }
    _http.end();

    String id = _doc["id"];
    String download = _doc["deployment"]["download"];
    String update = _doc["deployment"]["update"];

    return Deployment(id, download, update, chunks(_doc["deployment"]["chunks"]));
}

Stop HawkbitClient::readCancel(const String& href)
{
    _http.begin(this->_wifi, href);

    _http.addHeader("Authorization", this->_authToken);
    _http.addHeader("Accept", "application/hal+json");

    _doc.clear();

    int code = _http.GET();
    log_d("Result - code: %d", code);
    String resultPayload = _http.getString();
    log_d("Result - payload: %s", resultPayload.c_str());
    if ( code == HTTP_CODE_OK ) {
        DeserializationError error = deserializeJson(_doc, resultPayload);
        if (error) {
            _http.end();
            // FIXME: need a way to handle errors
            throw 1;
        }
    }
    _http.end();

    String stopId = _doc["cancelAction"]["stopId"] | "";
    
    return Stop(stopId);
}

String HawkbitClient::feedbackUrl(const Deployment& deployment) const
{
    return this->_baseUrl + "/" + this->_tenantName + "/controller/v1/" + this->_controllerId + "/deploymentBase/" + deployment.id() + "/feedback";
}

String HawkbitClient::feedbackUrl(const Stop& stop) const
{
    return this->_baseUrl + "/" + this->_tenantName + "/controller/v1/" + this->_controllerId + "/cancelAction/" + stop.id() + "/feedback";
}

template<typename IdProvider>
UpdateResult HawkbitClient::sendFeedback(IdProvider id, const String& execution, const String& finished, std::vector<String> details)
{
    _doc.clear();

    _doc["id"] = id.id();
    
    JsonArray d = _doc["status"].createNestedArray("details");
    for (auto detail : details) {
        d.add(detail);
    }

    _doc["status"]["execution"] = execution;
    _doc["status"]["result"]["finished"] = finished;

    _http.begin(this->_wifi, this->feedbackUrl(id));

    _http.addHeader("Accept", "application/hal+json");
    _http.addHeader("Content-Type", "application/json");
    _http.addHeader("Authorization", this->_authToken);

    String buffer;
    size_t len = serializeJson(_doc, buffer);
    (void)len; // ignore unused

    log_d("JSON - len: %d", len);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
    serializeJsonPretty(_doc, Serial);
#endif

    // FIXME: handle result
    int code = _http.POST(buffer);
    log_d("Result - code: %d", code);

    String resultPayload = _http.getString();
    log_d("Result - payload: %s", resultPayload.c_str());

    _http.end();

    return UpdateResult(code);
}

UpdateResult HawkbitClient::reportProgress(const Deployment& deployment, uint32_t done, uint32_t total, std::vector<String> details)
{
    return sendFeedback(
        deployment,
        "proceeding",
        "none",
        details
    );
}

UpdateResult HawkbitClient::reportScheduled(const Deployment& deployment, std::vector<String> details)
{
    return sendFeedback(
        deployment,
        "scheduled",
        "none",
        details
    );
}

UpdateResult HawkbitClient::reportResumed(const Deployment& deployment, std::vector<String> details)
{
    return sendFeedback(
        deployment,
        "resumed",
        "none",
        details
    );
}

UpdateResult HawkbitClient::reportComplete(const Deployment& deployment, bool success, std::vector<String> details)
{
    return sendFeedback(
        deployment,
        "closed",
        success ? "success" : "failure",
        details
    );
}

UpdateResult HawkbitClient::reportCanceled(const Deployment& deployment, std::vector<String> details)
{
    return sendFeedback(
        deployment,
        "canceled",
        "none",
        details
    );
}

UpdateResult HawkbitClient::reportCancelAccepted(const Stop& stop, std::vector<String> details)
{
    return sendFeedback(
        stop,
        "closed",
        "success",
        details
    );
}

UpdateResult HawkbitClient::reportCancelRejected(const Stop& stop, std::vector<String> details)
{
    return sendFeedback(
        stop,
        "closed",
        "failure",
        details
    );
}
