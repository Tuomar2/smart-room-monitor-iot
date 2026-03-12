/*
  COMP.CE.450-2025-2026-1
  Ryhmä 24

  ARDUINO --> RASPI
    - lämpötila (°C)
    - kosteus (%RH)
    - läheisyys (proximity lukema)

  RASPI --> ARDUINO
    - pwm (0..255)  -> "tuuletin/teho, PWM LED"
    - light (0/1)   -> valaistuksen ohjaus ML ohjaamana (LED)
    - alarm (0/1)   -> jokin hälytysvalo, ML? (LED)

*/
// määritellään tarvittavat kirjastot (BLE + anturikirjastot)
#include <ArduinoBLE.h>
#include <Arduino_HTS221.h>
#include <Arduino_APDS9960.h>

// Arduino lähdöt
const int PWM_PIN   = 9;   // PWM-led, joku "tuuletin"
const int LIGHT_PIN = 13;  // valaistuksen ohjaus raspin ML ohjaamana (LED)
const int ALARM_PIN = 11;  // jokin hälytysvalo, ML puolella määrittely? (LED)

// Määritetään jokin mittausväli (vakio) ja alustettu muuttuja ajan seuraamiseen
const unsigned long MEASURE_INTERVAL_MS = 2000;
unsigned long lastMeasure = 0;

// BLE määrittelyt, annetaan jokin UUID (ei vastaa todellista UUID formaattia)
BLEService bleService("bb1bda87-cdc5-4c04-b4a7-6d2bc5ba903b");

// Anturidata: 3x float arvoa(4 tavua/arvo) yhdessä paketissa, yhteensä 12 tavua
// BLERead antaa raspille oikeuden ainoastaan lukea, BLENotify lähettää datan raspin pyytämättä
// BLE charasteristicsit eli datastruktuureille aina kaikilla oma UUID, ei saa olla sama kuin palvelun UUID
BLECharacteristic mittausData("ed6629b5-3f81-4d4a-be67-e81d560fc69e", BLERead | BLENotify, 12);

// Ohjaus: pwm + 2 lediä, tavuja kaikki mutta kuvitellaan booleanien olevan 0=LOW/1=HIGH 
BLECharacteristic ohjausData("d3601b1c-013d-4fc2-9e65-ea619cf81131", BLEWrite, 3);


void setup()
{
  // Määritetään pinnit outputeiksi
  pinMode(PWM_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(ALARM_PIN, OUTPUT);

  // Alkuun kaikki pois päältä
  analogWrite(PWM_PIN, 0);
  digitalWrite(LIGHT_PIN, LOW);
  digitalWrite(ALARM_PIN, LOW);

  // Serial lähinnä debug tarkoitukseen
  Serial.begin(9600);
  delay(200);

  // "käynnistetään" anturit, ei alettu tekemään virhekäsittelyä jos ei käynnisty, demo pilalla anyway jos ei toimi
  HTS.begin();
  APDS.begin();

  // BLE alustus, looppi turhaa? Varmuuden vuoksi debug käyttöön ennen demoa
  while (!BLE.begin()) {
    Serial.println("BLE ei kaynnistynyt, yritetään uudelleen");
    delay(2000);
  }

  // määritellään lokaali ja laitenimi
  BLE.setLocalName("Nano333");
  BLE.setDeviceName("Nano33");
  BLE.setAdvertisedService(bleService);

  // lisätään mittausData ja ohjausData
  bleService.addCharacteristic(mittausData);
  bleService.addCharacteristic(ohjausData);

  // Lisätään BLE palvelu
  BLE.addService(bleService);

  // Alkuarvot mittausDataan, eli nollat kun BLE käynnistyy
  float data[3] = {0.0f, 0.0f, 0.0f};
  // koska BLE ei ilmeisesti käsitä mitään floateista, osoitetaan muistiosoite ja tavujen määrä
  mittausData.writeValue((byte*)data, sizeof(data));

  // laitetaan BLE löydettävään tilaan
  BLE.advertise();
}

void loop()
{
  // luodaan muuttuja nimeltä central tyypiltään BLEDevice
  // BLE.central() antaa arduinoon yhdistäneen laitteen tiedot
  BLEDevice central = BLE.central();

  // aloitetaan suoritus jos yhteys huomataan
  // central on tyhjä mikäli aikaisempi haku ei löytänyt yhdistänyttä laitetta
  if (central) {
    Serial.print("BLE yhdistetty: ");
    Serial.println(central.address());

    // jos yhteys katkeaa, lopetetaan while loop
    while (central.connected()) {

      // jos raspi kirjoittanut uuden ohjausviestin:
      if (ohjausData.written()) {
        // varataan 3 kpl tavuja taulukosta
        uint8_t bufferControl[3];
        // luetaan sisältö taulukon tavuihin ja int n on luettujen tavujen lukumäärä
        int n = ohjausData.readValue(bufferControl, 3);

        // kirjoitetaan lähdöt ainoastaan varmistettua tavujen oikea lukumäärä
        if (n == 3) {
          // kirjoitetaan PWM, valaisin ja hälytyslähtö
          analogWrite(PWM_PIN, bufferControl[0]);
          digitalWrite(LIGHT_PIN, bufferControl[1]);
          digitalWrite(ALARM_PIN, bufferControl[2]);

          // jotain debuggia taas serialiin
          Serial.print(" pwm=");
          Serial.print(bufferControl[0]);
          Serial.print(" light=");
          Serial.print(bufferControl[1]);
          Serial.print(" alarm=");
          Serial.println(bufferControl[2]);
        } else {
          Serial.println("Ohjausviesti virheellinen!");
        }
      }

      // Lähetetään anturidata alussa määritellyn ajan välein, funktio millis antaa ms muodossa ajan siitä kun arduino käynnistyi (overflow palauttaa aina toki nollaan)
      unsigned long timeNow = millis();
      if (timeNow - lastMeasure >= MEASURE_INTERVAL_MS) {
        lastMeasure = timeNow;

        // luetaan HTS kirjastolla sisäänrakennettujen antureiden lämpötila ja kosteus
        float temp = HTS.readTemperature();
        float humid = HTS.readHumidity();
        // alustetaan proximity ja kirjataan uusi etäisyys ainoastaan jos lukema saatavilla
        int proximity = 0;
        if (APDS.proximityAvailable()) {
          proximity = APDS.readProximity();
        }

        // Yhdistetään kolme lukua yhteen (3x float = 12 tavua kuten alussa määritelty)
        float data[3];
        data[0] = temp;
        data[1] = humid;
        data[2] = (float)proximity;

        // notify lähtee raspille automaattisesti koska määrittely mukana BLECharacteristic mittausData
        mittausData.writeValue((byte*)data, 12);

        // jotain debuggia taas serialiin
        Serial.print("Mittaus: T=");
        Serial.print(temp, 1);
        Serial.print("C RH=");
        Serial.print(humid, 1);
        Serial.print("% prox=");
        Serial.println(proximity);
      }
      delay(1000);
    }

    Serial.println("BLE yhteys katkennut");
    // laitetaan BLE löydettävään tilaan
    BLE.advertise();
  }
  Serial.println("BLE ei yhdistetty");
  delay(2000);
}