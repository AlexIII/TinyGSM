/**
 * @file       TinyGsmClientM95.h
 * @author     Volodymyr Shymanskyy, Replicade Ltd.
 * @license    LGPL-3.0
 * @copyright  Copyright (c) 2016 Volodymyr Shymanskyy, (c)2017 Replicade Ltd. <http://www.replicade.com>
 * @date       Nov 2016
 */

#ifndef TinyGsmClientM95_h
#define TinyGsmClientM95_h

// #define TINY_GSM_DEBUG Serial
// #define TINY_GSM_USE_HEX

#if !defined(TINY_GSM_RX_BUFFER)
  #define TINY_GSM_RX_BUFFER 64
#endif

#define TINY_GSM_MUX_COUNT 6

#include <TinyGsmCommon.h>

#define GSM_NL "\r\n"
static const char GSM_OK[] TINY_GSM_PROGMEM = "OK" GSM_NL;
static const char GSM_ERROR[] TINY_GSM_PROGMEM = "ERROR" GSM_NL;

enum SimStatus {
  SIM_ERROR = 0,
  SIM_READY = 1,
  SIM_LOCKED = 2,
};

enum RegStatus {
  REG_UNREGISTERED = 0,
  REG_SEARCHING    = 2,
  REG_DENIED       = 3,
  REG_OK_HOME      = 1,
  REG_OK_ROAMING   = 5,
  REG_UNKNOWN      = 4,
};

//============================================================================//
//============================================================================//
//                    Declaration of the TinyGsmM95 Class
//============================================================================//
//============================================================================//

class TinyGsmM95
{

//============================================================================//
//============================================================================//
//                          The M95 Client Class
//============================================================================//
//============================================================================//


public:

class GsmClient : public Client
{
  friend class TinyGsmM95;
  typedef TinyGsmFifo<uint8_t, TINY_GSM_RX_BUFFER> RxFifo;

public:
  GsmClient() {}

  GsmClient(TinyGsmM95& modem, uint8_t mux = 1) {
    init(&modem, mux);
  }

  bool init(TinyGsmM95* modem, uint8_t mux = 1) {
    this->at = modem;
    this->mux = mux;
    sock_available = 0;
    sock_connected = false;
    got_data = false;

    at->sockets[mux] = this;

    return true;
  }

public:
  virtual int connect(const char *host, uint16_t port) {
    stop();
    TINY_GSM_YIELD();
    rx.clear();
    sock_connected = at->modemConnect(host, port, mux);
    return sock_connected;
  }

  virtual int connect(IPAddress ip, uint16_t port) {
    String host; host.reserve(16);
    host += ip[0];
    host += ".";
    host += ip[1];
    host += ".";
    host += ip[2];
    host += ".";
    host += ip[3];
    return connect(host.c_str(), port);
  }

  virtual void stop() {
    TINY_GSM_YIELD();
    at->sendAT(GF("+QICLOSE="), mux);
    sock_connected = false;
    at->waitResponse(60000L, GF("CLOSED"), GF("CLOSE OK"), GF("ERROR"));
    rx.clear();
  }

  virtual size_t write(const uint8_t *buf, size_t size) {
    TINY_GSM_YIELD();
    at->maintain();
    return at->modemSend(buf, size, mux);
  }

  virtual size_t write(uint8_t c) {
    return write(&c, 1);
  }

  virtual int available() {
    TINY_GSM_YIELD();
    if (!rx.size()) {
      at->maintain();
    }
    return rx.size() + sock_available;
  }

  virtual int read(uint8_t *buf, size_t size) {
    TINY_GSM_YIELD();
    at->maintain();
    size_t cnt = 0;
    while (cnt < size) {
      size_t chunk = TinyGsmMin(size-cnt, rx.size());
      if (chunk > 0) {
        rx.get(buf, chunk);
        buf += chunk;
        cnt += chunk;
        continue;
      }
      // TODO: Read directly into user buffer?
      at->maintain();
      if (sock_available > 0) {
        at->modemRead(rx.free(), mux);
      } else {
        break;
      }
    }
    return cnt;
  }

  virtual int read() {
    uint8_t c;
    if (read(&c, 1) == 1) {
      return c;
    }
    return -1;
  }

  virtual int peek() { return -1; } //TODO
  virtual void flush() { at->stream.flush(); }

  virtual uint8_t connected() {
    if (available()) {
      return true;
    }
    return sock_connected;
  }
  virtual operator bool() { return connected(); }

  /*
   * Extended API
   */

  String remoteIP() TINY_GSM_ATTR_NOT_IMPLEMENTED;

private:
  TinyGsmM95*     at;
  uint8_t         mux;
  uint16_t        sock_available;
  bool            sock_connected;
  bool            got_data;
  RxFifo          rx;
};

//============================================================================//
//============================================================================//
//                          The M95 Has No Secure Client
//============================================================================//
//============================================================================//


//============================================================================//
//============================================================================//
//                          The M95 Modem Functions
//============================================================================//
//============================================================================//

public:

#ifdef GSM_DEFAULT_STREAM
  TinyGsmM95(Stream& stream = GSM_DEFAULT_STREAM)
#else
  TinyGsmM95(Stream& stream)
#endif
    : stream(stream)
  {
    memset(sockets, 0, sizeof(sockets));
  }

  /*
   * Basic functions
   */
  bool begin() {
    return init();
  }

  bool init() {
    if (!testAT()) {
      return false;
    }
    sendAT(GF("&FZE0"));  // Factory + Reset + Echo Off
    if (waitResponse() != 1) {
      return false;
    }
    getSimStatus();
    return true;
  }

  void setBaud(unsigned long baud) {
    sendAT(GF("+IPR="), baud);
  }

  bool testAT(unsigned long timeout = 10000L) {
    for (unsigned long start = millis(); millis() - start < timeout; ) {
      sendAT(GF(""));
      if (waitResponse(200) == 1) {
        delay(100);
        return true;
      }
      delay(100);
    }
    return false;
  }

  void maintain() {
    for (int mux = 0; mux < TINY_GSM_MUX_COUNT; mux++) {
      GsmClient* sock = sockets[mux];
      if (sock && sock->got_data) {
        sock->got_data = false;
        sock->sock_available = modemGetAvailable(mux);
      }
    }
    while (stream.available()) {
      waitResponse(10, NULL, NULL);
    }
  }

  bool factoryDefault() {
    sendAT(GF("&FZE0&W"));  // Factory + Reset + Echo Off + Write
    waitResponse();
    sendAT(GF("+IPR=0"));   // Auto-baud
    waitResponse();
    sendAT(GF("+IFC=0,0")); // No Flow Control
    waitResponse();
    sendAT(GF("+ICF=3,3")); // 8 data 0 parity 1 stop
    waitResponse();
    sendAT(GF("+CSCLK=0")); // Disable Slow Clock
    waitResponse();
    sendAT(GF("&W"));       // Write configuration
    return waitResponse() == 1;
  }

  String getModemInfo() {
    sendAT(GF("I"));
    String res;
    if (waitResponse(1000L, res) != 1) {
      return "";
    }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, " ");
    res.trim();
    return res;
  }

  bool hasSSL() {
    return false;  // TODO: For now
  }

  /*
   * Power functions
   */

  bool restart() {
    if (!testAT()) {
      return false;
    }
    sendAT(GF("+CFUN=0"));
    if (waitResponse(10000L, GF("NORMAL POWER DOWN"), GF("OK"), GF("FAIL")) == 3) {
      return false;
    }
    sendAT(GF("+CFUN=1"));
    if (waitResponse(10000L, GF("Call Ready"), GF("OK"), GF("FAIL")) == 3) {
      return false;
    }
    return init();
  }

  bool poweroff() {
    sendAT(GF("+QPOWD"));
    return waitResponse(GF("POWERED DOWN")) == 1; // TODO
  }

  bool radioOff() {
    sendAT(GF("+CFUN=0"));
    if (waitResponse(10000L) != 1) {
      return false;
    }
    delay(3000);
    return true;
  }

  /*
   * SIM card functions
   */

  bool simUnlock(const char *pin) {
    sendAT(GF("+CPIN=\""), pin, GF("\""));
    return waitResponse() == 1;
  }

  String getSimCCID() {
    sendAT(GF("+ICCID"));
    if (waitResponse(GF(GSM_NL "+ICCID:")) != 1) {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  String getIMEI() {
    sendAT(GF("+GSN"));
    if (waitResponse(GF(GSM_NL)) != 1) {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  SimStatus getSimStatus(unsigned long timeout = 10000L) {
    for (unsigned long start = millis(); millis() - start < timeout; ) {
      sendAT(GF("+CPIN?"));
      if (waitResponse(GF(GSM_NL "+CPIN:")) != 1) {
        delay(1000);
        continue;
      }
      int status = waitResponse(GF("READY"), GF("SIM PIN"), GF("SIM PUK"), GF("NOT INSERTED"));
      waitResponse();
      switch (status) {
        case 2:
        case 3:  return SIM_LOCKED;
        case 1:  return SIM_READY;
        default: return SIM_ERROR;
      }
    }
    return SIM_ERROR;
  }

  String getOperator() {
    sendAT(GF("+COPS?"));
    if (waitResponse(GF(GSM_NL "+COPS:")) != 1) {
      return "";
    }
    streamSkipUntil('"'); // Skip mode and format
    String res = stream.readStringUntil('"');
    waitResponse();
    return res;
  }

  /*
   * Generic network functions
   */

  RegStatus getRegistrationStatus() {
    sendAT(GF("+CREG?"));
    if (waitResponse(GF(GSM_NL "+CREG:")) != 1) {
      return REG_UNKNOWN;
    }
    streamSkipUntil(','); // Skip format (0)
    int status = stream.readStringUntil('\n').toInt();
    waitResponse();
    return (RegStatus)status;
  }

  int getSignalQuality() {
    sendAT(GF("+CSQ"));
    if (waitResponse(GF(GSM_NL "+CSQ:")) != 1) {
      return 99;
    }
    int res = stream.readStringUntil(',').toInt();
    waitResponse();
    return res;
  }

  bool isNetworkConnected() {
    RegStatus s = getRegistrationStatus();
    return (s == REG_OK_HOME || s == REG_OK_ROAMING);
  }

  bool waitForNetwork(unsigned long timeout = 60000L) {
    for (unsigned long start = millis(); millis() - start < timeout; ) {
      if (isNetworkConnected()) {
        return true;
      }
      delay(250);
    }
    return false;
  }

  void setHostFormat( bool useDottedQuad ) {
    if ( useDottedQuad ) {
      sendAT(GF("+QIDNSIP=0"));
    } else {
      sendAT(GF("+QIDNSIP=1"));
    }
    waitResponse();
  }

  String getLocalIP() {
    sendAT(GF("+CGPADDR=1"));
    if (waitResponse(10000L, GF(GSM_NL "+CGPADDR:")) != 1) {
      return "";
    }
    streamSkipUntil(',');
    String res = stream.readStringUntil('\n');
    if (waitResponse() != 1) {
      return "";
    }
    return res;
  }

  IPAddress localIP() {
    return TinyGsmIpFromString(getLocalIP());
  }

  /*
   * WiFi functions
   */

  /*
   * GPRS functions
   */
  bool gprsConnect(const char* apn, const char* user = NULL, const char* pwd = NULL) {
    gprsDisconnect();

    sendAT(GF("+QIFGCNT=0"));  // Set the forground context
    if (waitResponse() != 1) {
      return false;
    }

    sendAT(GF("+QICSGP=1,\""), apn, GF("\",\""), user, GF("\",\""), pwd, GF("\""));
    if (waitResponse() != 1) {
      return false;
    }

    sendAT(GF("+QIREGAPP"));
    if (waitResponse() != 1) {
      return false;
    }

    sendAT(GF("+QIACT"));
    waitResponse(10000L);

    return true;
  }

  bool gprsDisconnect() {
    sendAT(GF("+QIDEACT"));
    return waitResponse(60000L, GF("DEACT OK"), GF("ERROR")) == 1;
  }

  bool isGprsConnected() {
    sendAT(GF("+CGATT?"));
    if (waitResponse(GF(GSM_NL "+CGATT:")) != 1) {
      return false;
    }
    int res = stream.readStringUntil('\n').toInt();
    waitResponse();
    if (res != 1)
      return false;

    return localIP() != 0;
  }

  /*
   * Messaging functions
   */

  String sendUSSD(const String& code) TINY_GSM_ATTR_NOT_IMPLEMENTED;

  bool sendSMS(const String& number, const String& text) {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    sendAT(GF("+CMGS=\""), number, GF("\""));
    if (waitResponse(GF(">")) != 1) {
      return false;
    }
    stream.print(text);
    stream.write((char)0x1A);
    stream.flush();
    return waitResponse(60000L) == 1;
  }

  bool sendSMS_UTF16(const String& number, const void* text, size_t len) {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    sendAT(GF("+CSCS=\"HEX\""));
    waitResponse();
    sendAT(GF("+CSMP=17,167,0,8"));
    waitResponse();

    sendAT(GF("+CMGS=\""), number, GF("\""));
    if (waitResponse(GF(">")) != 1) {
      return false;
    }

    uint16_t* t = (uint16_t*)text;
    for (size_t i=0; i<len; i++) {
      uint8_t c = t[i] >> 8;
      if (c < 0x10) { stream.print('0'); }
      stream.print(c, HEX);
      c = t[i] & 0xFF;
      if (c < 0x10) { stream.print('0'); }
      stream.print(c, HEX);
    }
    stream.write((char)0x1A);
    stream.flush();
    return waitResponse(60000L) == 1;
  }

  void sendUSSD() {}

  void sendSMS() {}

  /** Delete all SMS */
  bool deleteAllSMS() {
    sendAT(GF("+QMGDA=6"));
    if (waitResponse(waitResponse(60000L, GF("OK"), GF("ERROR")) == 1) ) {
      return true;
    }
    return false;
  }


  /*
   * Location functions
   */
  void getLocation() {}

  String getGsmLocation() {
    sendAT(GF("+CIPGSMLOC=1,1"));
    if (waitResponse(GF(GSM_NL "+CIPGSMLOC:")) != 1) {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  /*
   * Battery functions
   */

  // Use: float vBatt = modem.getBattVoltage() / 1000.0;
  uint16_t getBattVoltage() {
    sendAT(GF("+CBC"));
    if (waitResponse(GF(GSM_NL "+CBC:")) != 1) {
      return 0;
    }
    streamSkipUntil(','); // Skip
    streamSkipUntil(','); // Skip

    uint16_t res = stream.readStringUntil(',').toInt();
    waitResponse();
    return res;
  }

  int getBattPercent() {
    sendAT(GF("+CBC"));
    if (waitResponse(GF(GSM_NL "+CBC:")) != 1) {
      return false;
    }
    stream.readStringUntil(',');
    int res = stream.readStringUntil(',').toInt();
    waitResponse();
    return res;
  }

protected:

  bool modemConnect(const char* host, uint16_t port, uint8_t mux) {
    sendAT(GF("+QIOPEN="), GF("\"TCP"), GF("\",\""), host, GF("\","), port);
    int rsp = waitResponse(75000L,
                 GF("CONNECT OK" GSM_NL),
                 GF("CONNECT FAIL" GSM_NL),
                 GF("ALREADY CONNECT" GSM_NL));
    if ( rsp != 1 ) {
      return false;
    }
    return (1 == rsp);
  }

  int modemSend(const void* buff, size_t len, uint8_t mux) {
    sendAT(GF("+QIHEAD=1"));
    waitResponse(5000L);

    sendAT(GF("+QISEND="), mux, ',', len);
    if (waitResponse(GF(">")) != 1) {
      return 0;
    }
    stream.write((uint8_t*)buff, len);
    stream.flush();
    if (waitResponse(GF(GSM_NL "SEND OK")) != 1) {
      return 0;
    }

    bool allAcknowledged = false;
    // bool failed = false;
    while ( !allAcknowledged ) {
      sendAT( GF("+QISACK"));
      if (waitResponse(5000L, GF(GSM_NL "+QISACK:")) != 1) {
        return -1;
      } else {
        streamSkipUntil(','); /** Skip total */
        streamSkipUntil(','); /** Skip acknowledged data size */
        if ( stream.readStringUntil('\n').toInt() == 0 ) {
          allAcknowledged = true;
        }
      }
    }
    waitResponse(5000L);

    // streamSkipUntil(','); // Skip mux
    // return stream.readStringUntil('\n').toInt();
    return 1;
  }

  size_t modemRead(size_t size, uint8_t mux) {
    sendAT(GF("+QIRD="), mux, ',', size);
    if (waitResponse(GF("+QIRD:")) != 1) {
      return 0;
    }
    size_t len = stream.readStringUntil('\n').toInt();

    for (size_t i=0; i<len; i++) {
      while (!stream.available()) { TINY_GSM_YIELD(); }
      char c = stream.read();
      sockets[mux]->rx.put(c);
    }
    waitResponse();
    DBG("### READ:", mux, ",", len);
    return len;
  }

  size_t modemGetAvailable(uint8_t mux) {
    sendAT(GF("+QIRD="), mux, GF(",0"));
    size_t result = 0;
    if (waitResponse(GF("+QIRD:")) == 1) {
      streamSkipUntil(','); // Skip total received
      streamSkipUntil(','); // Skip have read
      result = stream.readStringUntil('\n').toInt();
      DBG("### STILL:", mux, "has", result);
      waitResponse();
    }
    if (!result) {
      sockets[mux]->sock_connected = modemGetConnected(mux);
    }
    return result;
  }

  bool modemGetConnected(uint8_t mux) {
    sendAT(GF("+QISTATE=1,"), mux);
    //+QISTATE: 0,"TCP","151.139.237.11",80,5087,4,1,0,0,"uart1"

    if (waitResponse(GF("+QISTATE:")))
      return false;

    streamSkipUntil(','); // Skip mux
    streamSkipUntil(','); // Skip socket type
    streamSkipUntil(','); // Skip remote ip
    streamSkipUntil(','); // Skip remote port
    streamSkipUntil(','); // Skip local port
    int res = stream.readStringUntil(',').toInt(); // socket state

    waitResponse();

    // 0 Initial, 1 Opening, 2 Connected, 3 Listening, 4 Closing
    return 2 == res;
  }

public:

  /* Utilities */

  template<typename T>
  void streamWrite(T last) {
    stream.print(last);
  }

  template<typename T, typename... Args>
  void streamWrite(T head, Args... tail) {
    stream.print(head);
    streamWrite(tail...);
  }

  bool streamSkipUntil(char c) {
    const unsigned long timeout = 1000L;
    unsigned long startMillis = millis();
    while (millis() - startMillis < timeout) {
      while (millis() - startMillis < timeout && !stream.available()) {
        TINY_GSM_YIELD();
      }
      if (stream.read() == c)
        return true;
    }
    return false;
  }

  template<typename... Args>
  void sendAT(Args... cmd) {
    streamWrite("AT", cmd..., GSM_NL);
    stream.flush();
    TINY_GSM_YIELD();
    DBG("### AT:", cmd...);
  }

  // TODO: Optimize this!
  uint8_t waitResponse(uint32_t timeout, String& data,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    String r1s(r1); r1s.trim();
    String r2s(r2); r2s.trim();
    String r3s(r3); r3s.trim();
    String r4s(r4); r4s.trim();
    String r5s(r5); r5s.trim();
    DBG("### ..:", r1s, ",", r2s, ",", r3s, ",", r4s, ",", r5s);
    data.reserve(64);
    int index = 0;
    unsigned long startMillis = millis();
    do {
      TINY_GSM_YIELD();
      while (stream.available() > 0) {
        int a = stream.read();
        if (a <= 0) continue; // Skip 0x00 bytes, just in case
        data += (char)a;
        if (r1 && data.endsWith(r1)) {
          index = 1;
          goto finish;
        } else if (r2 && data.endsWith(r2)) {
          index = 2;
          goto finish;
        } else if (r3 && data.endsWith(r3)) {
          index = 3;
          goto finish;
        } else if (r4 && data.endsWith(r4)) {
          index = 4;
          goto finish;
        } else if (r5 && data.endsWith(r5)) {
          index = 5;
          goto finish;
        } else if (data.endsWith(GF(GSM_NL "+QIURC:"))) {
          stream.readStringUntil('\"');
          String urc = stream.readStringUntil('\"');
          stream.readStringUntil(',');
          if (urc == "recv") {
            int mux = stream.readStringUntil('\n').toInt();
            DBG("### URC RECV:", mux);
            if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
              sockets[mux]->got_data = true;
            }
          } else if (urc == "closed") {
            int mux = stream.readStringUntil('\n').toInt();
            DBG("### URC CLOSE:", mux);
            if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
              sockets[mux]->sock_connected = false;
            }
          } else {
            stream.readStringUntil('\n');
          }
          data = "";
        }
      }
    } while (millis() - startMillis < timeout);
finish:
    if (!index) {
      data.trim();
      if (data.length()) {
        DBG("### Unhandled:", data);
      }
      data = "";
    }
    DBG('<', index, '>');
    return index;
  }

  uint8_t waitResponse(uint32_t timeout,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    String data;
    return waitResponse(timeout, data, r1, r2, r3, r4, r5);
  }

  uint8_t waitResponse(GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    return waitResponse(1000, r1, r2, r3, r4, r5);
  }

public:
  Stream&       stream;

protected:
  GsmClient*    sockets[TINY_GSM_MUX_COUNT];
};

#endif