
// For debug purpose : disable SD logging or OLED display
#define USE_SD 1
#define USE_OLED 1

// Power meter module
#include <INA.h>
INA_Class INA;


//SD logger
#include "SdFat.h"
#define CS 4
#define SD_FREQ 50
char LOG_FILENAME[] =  "conso.csv";

SdFat SD;
File powerLog;
File presenceTest;
boolean SD_presence = false;
#define USE_COMMA 0 // remplace le '.' par une virgule ',' dans les décimaux, pour Excel



// OLED display
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#define OLED_ADDRESS 0x3C
#define OLED_RESET 5
SSD1306AsciiAvrI2c display;

#define BTN_PLUS   2
#define BTN_MINUS  3
#define RELAY_OUT  6
#define RELAY_HIGH LOW
#define RELAY_LOW HIGH



unsigned long previousMillis = 0;
unsigned int loops = 0;
#define INTERVAL 100

float shuntvoltage = 0;
float busvoltage = 0;
float current_mA = 0;
float loadvoltage = 0;
float energy = 0;
float Vcutoff = 3.0; // 3.0 volts (cellule lithium)
long Tcutoff = 600000; // 10 minutes

char cutoffMode = 'V'; // mode de coupure : V ou T

char buffer [16];

/**************************************************
 * Setup
 */
void setup() {
  Serial.begin(9600);
  Serial.println("Starting Power Meter");
  Serial.println("--------------------");

  pinMode(BTN_PLUS, INPUT_PULLUP);
  pinMode(BTN_MINUS, INPUT_PULLUP);
  
  pinMode(RELAY_OUT, OUTPUT);
  digitalWrite(RELAY_OUT, RELAY_HIGH);


  if (!SD.begin(CS, SD_SCK_MHZ(SD_FREQ))) {
    SD.errorPrint();
    Serial.println("Carte SD inaccessible :(");
  }else {
    Serial.println("Accès à la carte OK");
    SD_presence = true;
  }
  
  display.begin(&Adafruit128x64, OLED_ADDRESS, OLED_RESET);
  display.setFont(Adafruit5x7);

  Serial.println(F("Affichage OLED démarré"));
    
  displaySDPresence();

  int device = INA.begin(1,100000);         // Set expected Amps and resistor
                              // TODO : adjust using the other background sketch
                              
  INA.setBusConversion(8500);                                                 // Maximum conversion time 8.244ms  //
  INA.setShuntConversion(8500);                                               // Maximum conversion time 8.244ms  //
  INA.setAveraging(128);                                                      // Average each reading n-times     //
  INA.setMode(INA_MODE_CONTINUOUS_BOTH);                                      // Bus/shunt measured continuously  //
//  INA.AlertOnBusOverVoltage(true,5000);                                       // Trigger alert if over 5V on bus
}

//boolean checkSDPresence();

/**************************************************
 * Main loop
 */
void loop() {
  if (millis() - previousMillis >= INTERVAL)
  {

    if(cutoffMode == 'T'){
      Tcutoff -= (millis() - previousMillis);
    }
    
    previousMillis = millis();

    
    inavalues();

    checkButtons();
    checkCutoff();

      if ( checkSDPresence() ){
        storeData();
      }

      displaydata();
    
    loops++;
  }
}


/**
 * Vérifie le timer de cutoff ou la tension de cutoff (selon le mode sélectionné)
 * Garde le relais fermé si tout va bien, coupe le relais si le cutoff doit être déclenché
 */
void checkCutoff(){
  switch(cutoffMode){
    case 'T':   
      if(Tcutoff <=0){                      
        Tcutoff = 0;
        digitalWrite(RELAY_OUT, RELAY_LOW);
      }else {                               
        digitalWrite(RELAY_OUT, RELAY_HIGH);
      }
      break;
    case 'V':
      if(busvoltage < Vcutoff){
         digitalWrite(RELAY_OUT, RELAY_LOW);
      }else {
         digitalWrite(RELAY_OUT, RELAY_HIGH);
      }
    break;
  }
}


/**
 * Vérifie les appuis sur les boutons + et -
 * En mode cutoff timer : ajoute ou retire 10 minutes au timer 
 * En mode cutoff voltage : ajoute ou retire 0.1 V à la tension seuil
 * Appui simultané : passe d'un mode de cutoff à l'autre
 */
void checkButtons(){
  bool plus = !digitalRead(BTN_PLUS);
  bool minus = !digitalRead(BTN_MINUS);

  if(! (plus || minus) ){
    return;
  }else {
    delay(150);
  }

  plus = !digitalRead(BTN_PLUS);
  minus = !digitalRead(BTN_MINUS);

  if(plus && minus){
    cutoffMode = cutoffMode == 'T' ? 'V' : 'T';
    delay(300);
  }else if (plus){
    Serial.println("btn plus");

    switch(cutoffMode){
      case 'T' : 
      Tcutoff += 600000;
      Tcutoff = max(0, Tcutoff); // max : 99h
        break;
      case 'V':
      Vcutoff += 0.1;
      break;
    }
    
  }else if(minus){
    switch(cutoffMode){
      case 'T' : 
      Tcutoff -= 600000;
      Tcutoff = min(99 * 3600000, Tcutoff);
        break;
      case 'V':
      Vcutoff = max(0, Vcutoff - 0.1);
      break;
    }
    
  }

}

void storeData(){
    boolean alreadyCreated = SD.exists(LOG_FILENAME);

    powerLog = SD.open(LOG_FILENAME, FILE_WRITE);
    
    if (powerLog) {
      if(!alreadyCreated){
          powerLog.println(F("time (ms);loadVoltage (mV);current (mA);energy (mWh);capacity (mAh)"));
          
      }

      String row = (String(millis()) + ";" +  String(loadvoltage, 3) + ";" + String(current_mA, 3) + ";" + String(energy, 3) + ";" + String(energy/loadvoltage, 3));

      #if USE_COMMA
        //row = row.replace('.', ',');
        //powerLog.println( String(row.replace(".", ",") );
      #else
        powerLog.println( row);
      #endif
      
      
      Serial.println(row);
      
      powerLog.close();
    }
}

/**
 * Vérifie la présence d'une carte SD
 * Notifie l'utilisateur sur un changement
 * Demande la reprise ou l'écrasement d'un log présent si changement
 * Met à jour SD_presence
 */
boolean checkSDPresence(){
  long duree = millis();

  // Si la carte est de base absente, vérifier tous les X cycles si elle a été insérée
  if( !SD_presence ){
    
    if(loops % 100 == 0){
      boolean inserted = SD.begin(CS, SD_SCK_MHZ(SD_FREQ));
      
      if(inserted){
        Serial.println(F("Insérée !"));
        SD_presence = true;
        displaySDPresence(); // si carte insérée, afficher "Carte SD insérée"
//        if(SD.exists(LOG_FILENAME)){
//          Serial.println("Alerte : " + String(LOG_FILENAME) + " existe deja");
//        }
        
      }
    }
    
  }

  // Si la carte est de base présente, vérifier si elle a été retirée
  else {
    presenceTest = SD.open("presence.test", FILE_WRITE);
    int tries = 2;

    // On a droit à X essais pour éviter les faux négatifs
    while(!presenceTest && tries > 0){
//      SD.begin(CS, SD_SCK_MHZ(SD_FREQ));
      
      presenceTest = SD.open("presence.test", FILE_WRITE);
      tries--;  
    }
    
    if (!presenceTest) {
      SD_presence = false;
      Serial.println(F("SD retirée"));
      displaySDPresence(); // si carte retirée, afficher "Carte SD retirée"
      Serial.println(F("displayed SD presence"));
    }else{
      presenceTest.close();  
    }
    
  }

  duree = millis() - duree;
  Serial.println("SD Presence a duré " + String( duree ) + " ms");
  
  return SD_presence;
}

/**
 * Affiche les différentes mesures de tension / courant
 * Appelle aussi l'affichage des infos secondaires
 */
void displaydata() {
  //Serial.println("Displaying data");
  const int valsPadding = 31; // marge à gauche des valeurs
  const int unitsPadding = valsPadding + 8*6; // marge à gauche des unités

  if(loops % 100 == 0){ // assurer le rafraichissement complet de l'écran de temps en temps
    display.clear();  
  }
  

  // Rafraichissement économique : zones spécifiques only
  display.clear( 5, 0, 15, 3 ); // Zone des chiffres et unités
  if(loops % 10 == 0){
    display.clearField(0, 7, 13); // Ligne du bas
  }
  
  
  displayBattery();
  displayTime();
  displayCutoff();
  displaySD(SD_presence);
        
  display.setCursor(valsPadding, 0);
  display.println(loadvoltage, 3);
  display.setCursor(unitsPadding, 0);
  display.print("  V");

  display.setCursor(unitsPadding, 1);
  display.print(current_mA > 1000 ? " A" : " mA");

  display.setCursor(unitsPadding, 2);
  display.print(loadvoltage * current_mA > 10000 ? " W" : " mW");

  display.setCursor(unitsPadding, 3);
  display.println(energy > 10000 ? " Wh" : "mWh");

  display.setCursor(valsPadding, 1);
  display.println(current_mA > 1000 ? current_mA / 1000 : current_mA, 3);
  
  
  display.setCursor(valsPadding, 2);
  display.println(loadvoltage * current_mA > 10000 ? (loadvoltage * current_mA) / 1000 : loadvoltage * current_mA, 3);
  
  display.setCursor(valsPadding, 3);
  display.println(energy > 10000 ? energy/1000 : energy, 3);
}

/**
 * Info secondaire
 * Affiche l'heure depuis le démarrage du module
 */
void displayTime(){
  int hours = millis() / 3600000;
  int minutes = millis() / 60000 % 60;
  int seconds = millis() / 1000 % 60;

  snprintf(buffer,sizeof(buffer),"%02d:%02d:%02d", hours, minutes, seconds);
  
  display.setCursor(0, 5);
  display.println(buffer);
}

/**
 * Info secondaire
 * Affiche un petit message de statut pour donner l'état de cutoff
 */
void displayBattery(){
  display.setCursor(0, 7);

  switch(cutoffMode){
    case 'T':
      if(Tcutoff <= 0){
        display.println("Timer cutoff !");
      }else {
        display.println("Countdown OK");
      }
      break;
    case 'V':
      if(busvoltage < Vcutoff){
        display.println("Volt cutoff !");
      }else {
        display.println("Volt OK");
      }
      break;
  }

}

/**
 * Affiche un petit statut d'insertion de la carte "OK" ou "??"
 */
void displaySD(boolean presence){
  display.setCursor(92, 7);
  if(presence){
    display.print(F("SD:OK"));
  }else {
    display.print(F("SD:??"));
  }
  
}

/**
 * Affiche le tension paramétrée pour le cut-off
 * Si la tension surveillée tombe sous ce niveau, le relais coupe la batterie
 * et donc empêche sa décharge profonde
 */
void displayCutoff(){  
  display.setCursor(64, 5);
  switch(cutoffMode){
    case 'T':
      display.print("Time:" + String(Tcutoff / 3600000) + "h" + (Tcutoff / 60000 % 60 > 9 ? "" : "0") +  String(Tcutoff / 60000 % 60));
      break;
    case 'V':
      display.print("C/O:" + String(Vcutoff) + " V");  
      break;
  }
  
}

/**
 * Affiche un gros message "Carte SD insérée" ou "retirée"
 * Utilise la variable globale SD_presence
 */
void displaySDPresence(){
  display.clear();
  display.setCursor(15, 2);
  display.set2X();
  display.println( "Carte SD\n" +  String(SD_presence ? " inseree" : " retiree") );
  display.set1X();
  delay(1000);
  display.clear();
  
}

/**
 * Met à jour les valeurs de courant et tension
 * issues du capteur INA219 ou INA226
 */
void inavalues() {
  shuntvoltage = INA.getShuntMicroVolts(0)/1000.0;    // en mV
  busvoltage = INA.getBusMilliVolts(0)/1000.0;        // en mV
  current_mA = INA.getBusMicroAmps(0)/1000.0;         // en mA
  loadvoltage = busvoltage + (shuntvoltage / 1000);   // en mV
  energy = energy + loadvoltage * current_mA / 3600;  // en mWh
}




