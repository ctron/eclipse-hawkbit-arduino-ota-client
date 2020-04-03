#include <WiFi.h>
#include <WiFiMulti.h>

#include <hawkbit.h>
#include <Update.h>
#include <ArduinoJson.h>

#define VERSION "1.0.0"

WiFiMulti wifi;
EspClass esp;
WiFiClientSecure client;
StaticJsonDocument<16*1024> doc;

#define STRINGIFY(x) #x
HawkbitClient update(doc, client, STRINGIFY(HAWKBIT_URL), STRINGIFY(HAWKBIT_TENANT), STRINGIFY(HAWKBIT_DEVICE_ID), STRINGIFY(HAWKBIT_DEVICE_TOKEN));

const char * root_ca = "-----BEGIN CERTIFICATE-----\n\
MIIDSjCCAjKgAwIBAgIQRK+wgNajJ7qJMDmGLvhAazANBgkqhkiG9w0BAQUFADA/\n\
MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\n\
DkRTVCBSb290IENBIFgzMB4XDTAwMDkzMDIxMTIxOVoXDTIxMDkzMDE0MDExNVow\n\
PzEkMCIGA1UEChMbRGlnaXRhbCBTaWduYXR1cmUgVHJ1c3QgQ28uMRcwFQYDVQQD\n\
Ew5EU1QgUm9vdCBDQSBYMzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB\n\
AN+v6ZdQCINXtMxiZfaQguzH0yxrMMpb7NnDfcdAwRgUi+DoM3ZJKuM/IUmTrE4O\n\
rz5Iy2Xu/NMhD2XSKtkyj4zl93ewEnu1lcCJo6m67XMuegwGMoOifooUMM0RoOEq\n\
OLl5CjH9UL2AZd+3UWODyOKIYepLYYHsUmu5ouJLGiifSKOeDNoJjj4XLh7dIN9b\n\
xiqKqy69cK3FCxolkHRyxXtqqzTWMIn/5WgTe1QLyNau7Fqckh49ZLOMxt+/yUFw\n\
7BZy1SbsOFU5Q9D8/RhcQPGX69Wam40dutolucbY38EVAjqr2m7xPi71XAicPNaD\n\
aeQQmxkqtilX4+U9m5/wAl0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNV\n\
HQ8BAf8EBAMCAQYwHQYDVR0OBBYEFMSnsaR7LHH62+FLkHX/xBVghYkQMA0GCSqG\n\
SIb3DQEBBQUAA4IBAQCjGiybFwBcqR7uKGY3Or+Dxz9LwwmglSBd49lZRNI+DT69\n\
ikugdB/OEIKcdBodfpga3csTS7MgROSR6cz8faXbauX+5v3gTt23ADq1cEmv8uXr\n\
AvHRAosZy5Q6XkjEGB5YGV8eAlrwDPGxrancWYaLbumR9YbK+rlmM6pZW87ipxZz\n\
R8srzJmwN0jP41ZL9c8PDHIyh8bwRLtTcm1D9SZImlJnt1ir/md2cXjbDaJWFBM5\n\
JDGFoqgCWjBH4d1QB7wCCZAA62RjYJsWvIjJEubSfZGL+T0yjWW06XyxV3bqxbYo\n\
Ob8VZRzI9neWagqNdwvYkQsEjgfbKbYK7p2CNTUQ\n\
-----END CERTIFICATE-----\n\
";


// Not sure if WiFiClientSecure checks the validity date of the certificate. 
// Setting clock just to be sure...
void setClock() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print(F("Waiting for NTP time sync: "));
  time_t nowSecs = time(nullptr);
  while (nowSecs < 8 * 3600 * 2) {
    delay(500);
    Serial.print(F("."));
    yield();
    nowSecs = time(nullptr);
  }

  Serial.println();
  struct tm timeinfo;
  gmtime_r(&nowSecs, &timeinfo);
  Serial.print(F("Current time: "));
  Serial.print(asctime(&timeinfo));
}

void setup()
{
    Serial.begin(115200);
    delay(50);

    wifi.addAP("muenchen.freifunk.net/muc_ost");

    Serial.println();
    Serial.println();
    Serial.print("Waiting for WiFi... ");

    while(wifi.run() != WL_CONNECTED) {
        Serial.print(".");
        delay(1000);
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    setClock();
    client.setCACert(root_ca);
}

void processUpdate(const Deployment& deployment) {
  if (deployment.chunks().size() != 1) {
    throw String("Expect update to have one chunk");
  }

  const Chunk& chunk = deployment.chunks().front();
  if (chunk.artifacts().size() != 1 ) {
    throw String("Expect update to have one artifact");
  }

  const Artifact& artifact = chunk.artifacts().front();

  try {

    update.download(artifact, [artifact, deployment](Download& d){

      // begin update

      if (!Update.begin(artifact.size())) {
        if(Update.hasError()) {
          throw String(Update.errorString());
        } else {
          throw String("Failed to start update");
        }
      }

      // fetch checksum
      auto md5 = artifact.hashes().find("md5");

      // we have a checksum
      if(md5 != artifact.hashes().end()) {
        Update.setMD5(md5->second.c_str());
      }

      // write update
      Update.writeStream(d.stream());

      if(!Update.end()) {
        if(Update.hasError()) {
          throw String(Update.errorString());
        } else {
          throw String("Failed to end update");
        }
      }
    }, "download-http");

  }
  catch ( DownloadError err ) {
    // download failed, we can re-try
    log_w("Failed to download new firmware: %d", err.code());
    return;
  }

  // all done

  update.reportComplete(deployment, true);

  esp.restart();

}

void loop()
{
    log_d("Start loop");

    try {

      log_d("readState");

      State current = update.readState();
      current.dump(Serial);

      switch(current.type())
      {
        case State::NONE:
        {
          log_d("No update pending");
          break;
        }
        case State::REGISTER:
        {
          log_i("Need to register");
          update.updateRegistration(current.registration(), {
            {"mac", WiFi.macAddress()},
            {"app.version", VERSION},
            {"esp", "esp32"},
            {"esp32.chipRevision", String(esp.getChipRevision())},
            {"esp32.sdkVersion", esp.getSdkVersion()}
          });
          break;
        }
        case State::UPDATE:
        {
          const Deployment& deployment = current.deployment();
          update.reportProgress(deployment, 1, 2);
          try {
            processUpdate(deployment);
          }
          catch (const String& error) {
            update.reportComplete(deployment, false, {error});
          }

          break;
        }
        case State::CANCEL:
        {
          update.reportCancelAccepted(current.stop());
          break;
        }
      }
    }
    catch (int err) {
      log_e("Failed to fetch update information: %d", err);
    }

    log_i("End loop");

    delay(30000);
}

