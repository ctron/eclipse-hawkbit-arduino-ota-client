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
#include <ArduinoJson.h>

StaticJsonDocument<16*1024> doc;

HawkbitClient::HawkbitClient(
    WiFiClient& wifi,
    const String& baseUrl,
    const String& tenantName,
    const String& controllerId,
    const String &securityToken) :
    _wifi(wifi),
    _baseUrl(baseUrl),
    _tenantName(tenantName),
    _controllerId(controllerId),
    _authToken("TargetToken " + securityToken)
{
}

UpdateResult HawkbitClient::updateRegistration(const Registration& registration, const std::map<String,String>& data, MergeMode mergeMode, std::initializer_list<String> details)
{
    doc.clear();

    switch(mergeMode) {
        case MERGE:
            doc["mode"] = "merge";
            break;
        case REPLACE:
            doc["mode"] = "replace";
            break;
        case REMOVE:
            doc["mode"] = "remove";
            break;
    }

    doc.createNestedObject("data");
    doc["data"]["mac"] = WiFi.macAddress();
    for (const std::pair<String,String>& entry : data) {
        doc["data"][String(entry.first)] = entry.second;
    }

    JsonArray d = doc["status"].createNestedArray("details");
    for (auto detail : details) {
        d.add(detail);
    }

    doc["status"]["execution"] = "closed";
    doc["status"]["result"]["finished"] = "success";

    _http.begin(this->_wifi, registration.url());

    _http.addHeader("Accept", "application/hal+json");
    _http.addHeader("Content-Type", "application/json");
    _http.addHeader("Authorization", this->_authToken);

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
    serializeJsonPretty(doc, Serial);
#endif

    String buffer;
    size_t len = serializeJson(doc, buffer);

    log_i("JSON - len: %d", len);

    int code = _http.PUT(buffer);
    log_i("Result - code: %d", code);

    String resultPayload = _http.getString();
    log_i("Result - payload: %s", resultPayload.c_str());

    _http.end();

    return UpdateResult(code);
}

State HawkbitClient::readState()
{
    _http.begin(this->_wifi, this->_baseUrl + "/" + this->_tenantName + "/controller/v1/" + this->_controllerId);

    _http.addHeader("Authorization", this->_authToken);
    _http.addHeader("Accept", "application/hal+json");

    int code = _http.GET();
    log_i("Result - code: %d", code);
    String resultPayload = _http.getString();
    log_i("Result - payload: %s", resultPayload.c_str());
    if ( code == HTTP_CODE_OK ) {
        DeserializationError error = deserializeJson(doc, resultPayload);
        if (error) {
            // FIXME: need a way to handle errors
            throw 1;
        }
    }
    _http.end();

    String href = doc["_links"]["deploymentBase"]["href"] | "";
    if (!href.isEmpty()) {
        log_i("Fetching deployment: %s", href.c_str());
        return State(this->readDeployment(href));
    }

    href = doc["_links"]["configData"]["href"] | "";
    if (!href.isEmpty()) {
        log_i("Need to register", href.c_str());
        return State(Registration(href));
    }

    href = doc["_links"]["cancelAction"]["href"] | "";
    if (!href.isEmpty()) {
        log_i("Fetching cancel action: %s", href.c_str());
        return State(this->readCancel(href));
    }

    log_i("No update");
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

    int code = _http.GET();
    log_i("Result - code: %d", code);
    String resultPayload = _http.getString();
    log_i("Result - payload: %s", resultPayload.c_str());
    if ( code == HTTP_CODE_OK ) {
        DeserializationError error = deserializeJson(doc, resultPayload);
        if (error) {
            // FIXME: need a way to handle errors
            throw 1;
        }
    }
    _http.end();

    String id = doc["id"];
    String download = doc["deployment"]["download"];
    String update = doc["deployment"]["update"];

    return Deployment(id, download, update, chunks(doc["deployment"]["chunks"]));
}

Stop HawkbitClient::readCancel(const String& href)
{
    _http.begin(this->_wifi, href);

    _http.addHeader("Authorization", this->_authToken);
    _http.addHeader("Accept", "application/hal+json");

    int code = _http.GET();
    log_i("Result - code: %d", code);
    String resultPayload = _http.getString();
    log_i("Result - payload: %s", resultPayload.c_str());
    if ( code == HTTP_CODE_OK ) {
        DeserializationError error = deserializeJson(doc, resultPayload);
        if (error) {
            // FIXME: need a way to handle errors
            throw 1;
        }
    }
    _http.end();

    String stopId = doc["cancelAction"]["stopId"] | "";
    
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
UpdateResult HawkbitClient::sendFeedback(IdProvider id, const String& execution, const String& finished, std::initializer_list<String> details)
{
    doc.clear();

    doc["id"] = id.id();
    
    JsonArray d = doc["status"].createNestedArray("details");
    for (auto detail : details) {
        d.add(detail);
    }

    doc["status"]["execution"] = execution;
    doc["status"]["result"]["finished"] = finished;

    _http.begin(this->_wifi, this->feedbackUrl(id));

    _http.addHeader("Accept", "application/hal+json");
    _http.addHeader("Content-Type", "application/json");
    _http.addHeader("Authorization", this->_authToken);

    String buffer;
    size_t len = serializeJson(doc, buffer);

    log_i("JSON - len: %d", len);
    serializeJsonPretty(doc, Serial);

    // FIXME: handle result
    int code = _http.POST(buffer);
    log_i("Result - code: %d", code);

    String resultPayload = _http.getString();
    log_i("Result - payload: %s", resultPayload.c_str());

    _http.end();

    return UpdateResult(code);
}

UpdateResult HawkbitClient::reportProgress(const Deployment& deployment, uint32_t done, uint32_t total, std::initializer_list<String> details)
{
    return sendFeedback(
        deployment,
        "proceeding",
        "none",
        details
    );
}

UpdateResult HawkbitClient::reportScheduled(const Deployment& deployment, std::initializer_list<String> details)
{
    return sendFeedback(
        deployment,
        "scheduled",
        "none",
        details
    );
}

UpdateResult HawkbitClient::reportResumed(const Deployment& deployment, std::initializer_list<String> details)
{
    return sendFeedback(
        deployment,
        "resumed",
        "none",
        details
    );
}

UpdateResult HawkbitClient::reportComplete(const Deployment& deployment, bool success, std::initializer_list<String> details)
{
    return sendFeedback(
        deployment,
        "closed",
        success ? "success" : "failure",
        details
    );
}

UpdateResult HawkbitClient::reportCancelAccepted(const Stop& stop, std::initializer_list<String> details)
{
    return sendFeedback(
        stop,
        "canceled",
        "none",
        details
    );
}

UpdateResult HawkbitClient::reportCancelRejected(const Stop& stop, std::initializer_list<String> details)
{
    return sendFeedback(
        stop,
        "rejected",
        "none",
        details
    );
}
