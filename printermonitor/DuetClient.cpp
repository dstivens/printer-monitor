/** The MIT License (MIT)

Copyright (c) 2018 David Payne

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// Additional Contributions:

#include "DuetClient.h"

DuetClient::DuetClient(String ApiKey, String server, int port, String user, String pass, boolean psu) {
  updatePrintClient(ApiKey, server, port, user, pass, psu);
}

void DuetClient::updatePrintClient(String ApiKey, String server, int port, String user, String pass, boolean psu) {
  server.toCharArray(myServer, 100);
  myPort = port;
  encodedAuth = "";
  if (user != "") {
    String userpass = user + ":" + pass;
    base64 b64;
    encodedAuth = b64.encode(userpass, true);
  }
  pollPsu = psu;
}

boolean DuetClient::validate() {
  boolean rtnValue = false;
  printerData.error = "";
  if (String(myServer) == "") {
    printerData.error += "Server address is required; ";
  }
  if (printerData.error == "") {
    rtnValue = true;
  }
  return rtnValue;
}

WiFiClient DuetClient::getSubmitRequest(String apiGetData) {
  WiFiClient printClient;
  printClient.setTimeout(5000);

  Serial.println("Getting Duet Data via GET");
  Serial.println(apiGetData);
  result = "";
  if (printClient.connect(myServer, myPort)) {  //starts client connection, checks for connection
    printClient.println(apiGetData);
    printClient.println("Host: " + String(myServer) + ":" + String(myPort));
    if (encodedAuth != "") {
      printClient.print("Authorization: ");
      printClient.println("Basic " + encodedAuth);
    }
    printClient.println("User-Agent: ArduinoWiFi/1.1");
    printClient.println("Connection: close");
    if (printClient.println() == 0) {
      Serial.println("Connection to " + String(myServer) + ":" + String(myPort) + " failed.");
      Serial.println();
      resetPrintData();
      printerData.error = "Connection to " + String(myServer) + ":" + String(myPort) + " failed.";
      return printClient;
    }
  } 
  else {
    Serial.println("Connection to Duet failed: " + String(myServer) + ":" + String(myPort)); //error message if no client connect
    Serial.println();
    resetPrintData();
    printerData.error = "Connection to Duet failed: " + String(myServer) + ":" + String(myPort);
    return printClient;
  }

  // Check HTTP status
  char status[32] = {0};
  printClient.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0 && strcmp(status, "HTTP/1.1 409 CONFLICT") != 0) {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    printerData.state = "";
    printerData.error = "Response: " + String(status);
    return printClient;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!printClient.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    printerData.error = "Invalid response from " + String(myServer) + ":" + String(myPort);
    printerData.state = "";
  }

  return printClient;
}

void DuetClient::getPrinterJobResults() {
  if (!validate()) {
    return;
  }
  //**** get the Printer Job status
  String apiGetData = "GET /rr_status?type=3";
  WiFiClient printClient = getSubmitRequest(apiGetData);
  if (printerData.error != "") {
    return;
  }
  const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) + 2*JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(6) + 710;
  DynamicJsonBuffer jsonBuffer(bufferSize);

  // Parse JSON object
  JsonObject& root = jsonBuffer.parseObject(printClient);
  if (!root.success()) {
    Serial.println("Duet Data Parsing failed: " + String(myServer) + ":" + String(myPort));
    resetPrintData();
    printerData.error = "Duet Data Parsing failed: " + String(myServer) + ":" + String(myPort);
    return;
  }
  
  printerData.progressCompletion = (const char*)root["fractionPrinted"];
  printerData.progressFilepos = (const char*)root["filePosition"];
  printerData.progressPrintTime = (const char*)root["printDuration"];
  printerData.progressPrintTimeLeft = (const char*)root["timesLeft"]["filament"];
  printerData.state = (const char*)root["status"];
  printerData.toolTemp = (const char*)root["temps"]["current"][1];
  printerData.toolTargetTemp = (const char*)root["tools"]["active"];
  printerData.bedTemp = (const char*)root["temps"]["bed"]["current"];
  printerData.bedTargetTemp = (const char*)root["temps"]["bed"]["active"];

  if (isOperational()) {
    Serial.println("Status: " + printerData.state);
  } else {
    Serial.println("Printer Not Operational");
  } 

  //**** get the printing file data
  apiGetData = "GET /rr_fileinfo";
  printClient = getSubmitRequest(apiGetData);
  if (printerData.error != "") {
    return;
  }
  const size_t bufferSize2 = 3*JSON_OBJECT_SIZE(2) + 2*JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(9) + 300;
  DynamicJsonBuffer jsonBuffer2(bufferSize2);

  // Parse JSON object
  JsonObject& root2 = jsonBuffer2.parseObject(printClient);
  if (!root2.success()) {
    Serial.println("Duet Data Parsing failed: " + String(myServer) + ":" + String(myPort));
    resetPrintData();
    printerData.error = "Duet Data Parsing failed: " + String(myServer) + ":" + String(myPort);
    return;
  }
  
  printerData.fileName = (const char*)root2["fileName"];
  printerData.fileSize = (const char*)root2["size"];
  printerData.filamentLength = (const char*)root2["filament"][0];
}

void DuetClient::getPrinterPsuState() {
}

// Reset all PrinterData
void DuetClient::resetPrintData() {
  printerData.averagePrintTime = "";
  printerData.estimatedPrintTime = "";
  printerData.fileName = "";
  printerData.fileSize = "";
  printerData.lastPrintTime = "";
  printerData.progressCompletion = "";
  printerData.progressFilepos = "";
  printerData.progressPrintTime = "";
  printerData.progressPrintTimeLeft = "";
  printerData.state = "";
  printerData.toolTemp = "";
  printerData.toolTargetTemp = "";
  printerData.filamentLength = "";
  printerData.bedTemp = "";
  printerData.bedTargetTemp = "";
  printerData.isPrinting = false;
  printerData.isPSUoff = false;
  printerData.error = "";
}

String DuetClient::getAveragePrintTime(){
  return printerData.averagePrintTime;
}

String DuetClient::getEstimatedPrintTime() {
  return printerData.estimatedPrintTime;
}

String DuetClient::getFileName() {
  return printerData.fileName;
}

String DuetClient::getFileSize() {
  return printerData.fileSize;
}

String DuetClient::getLastPrintTime(){
  return printerData.lastPrintTime;
}

String DuetClient::getProgressCompletion() {
  return String(printerData.progressCompletion.toInt());
}

String DuetClient::getProgressFilepos() {
  return printerData.progressFilepos;  
}

String DuetClient::getProgressPrintTime() {
  return printerData.progressPrintTime;
}

String DuetClient::getProgressPrintTimeLeft() {
  String rtnValue = printerData.progressPrintTimeLeft;
  if (getProgressCompletion() == "100") {
    rtnValue = "0"; // Print is done so this should be 0 this is a fix for Duet
  }
  return rtnValue;
}

String DuetClient::getState() {
  switch(printerData.state[0])
  {
    case 'C':
    case 'I':
    case 'B':
    case 'P':
    case 'D':
    case 'S':
    case 'R':
    case 'H':
    case 'F':
    case 'T':
        return "Operational";
    break;
    default:
        return "Offline";
    break;
  }
}

boolean DuetClient::isPrinting() {
  switch(printerData.state[0])
  {
    case 'P':
    case 'D':
      printerData.isPrinting = true;
    break;
    case 'C':
    case 'I':
    case 'B':
    case 'S':
    case 'R':
    case 'H':
    case 'F':
    case 'T':
    default:
      printerData.isPrinting = false;
    break;
  }
  return printerData.isPrinting;
}

boolean DuetClient::isPSUoff() {
  return printerData.isPSUoff;
}

boolean DuetClient::isOperational() {
  boolean operational = false;
  switch(printerData.state[0])
  {
    case 'C':
    case 'I':
    case 'B':
    case 'P':
    case 'D':
    case 'S':
    case 'R':
    case 'H':
    case 'F':
    case 'T':
        operational = true;
    break;
  }
  if (operational || isPrinting()) {
    operational = true;
  }
  return operational;
}

String DuetClient::getTempBedActual() {
  return printerData.bedTemp;
}

String DuetClient::getTempBedTarget() {
  return printerData.bedTargetTemp;
}

String DuetClient::getTempToolActual() {
  return printerData.toolTemp;
}

String DuetClient::getTempToolTarget() {
  return printerData.toolTargetTemp;
}

String DuetClient::getFilamentLength() {
  return printerData.filamentLength;
}

String DuetClient::getError() {
  return printerData.error;
}

String DuetClient::getValueRounded(String value) {
  float f = value.toFloat();
  int rounded = (int)(f+0.5f);
  return String(rounded);
}

String DuetClient::getPrinterType() {
  return printerType;
}

int DuetClient::getPrinterPort() {
  return myPort;
}

String DuetClient::getPrinterName() {
  return printerData.printerName;
}

void DuetClient::setPrinterName(String printer) {
  printerData.printerName = printer;
}
