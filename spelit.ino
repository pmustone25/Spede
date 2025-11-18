volatile bool timerkOn = false; //merkkilippu leditimerkeskeytykselle, true: timerkeskeytys tapahtunut
volatile bool endGame = false; //merkkilippu lopetukselle, true: näppäintä väärinpainettu, lopeta peli
volatile bool nappiaPainettu = false; //merkkilippu näytönpävitykselle, true: päivinä näyttöön tulosta
//määritellään nämä globaaleiksi yksinkertaisuuden vuoksi, 100 ledi/nappijärjestyksen muisti siis
volatile uint8_t ledsSeq[100];// = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
volatile uint8_t buttonsSeq[100];// = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
volatile uint8_t buttonCount = 0;
volatile uint8_t ledCount = 0;
//debouncea varten eli poistetaan nappivärähtely tämän aikamuuttujan avulla
volatile unsigned long lastPress = 0;

//prototyypit perusfunktioille
void enableLed(void);
uint8_t checkGameResult(void);
void updateScreen(uint8_t);

//muistia helpottavat S2P määrittelyt
#define dataPin 7
#define clockPin 12
#define latchPin 13

//7-segmenttinäytön numeroiden lähetys sarjamuodossa, nämä saatu kokeilemalla
//yksi segmentti ei syty, oletettavasti 128 = ylävaakaviiva, ja 32 = desimaalipiste; 128 tuottaa sekavuutta näytölle
uint8_t numerotAsBitsToSerial[10] = {93, 68, 90, 78, 71, 15, 31, 25 /*nurinpäin */, 95, 79};

// 1: pysty, vasen yläkulma  2: keskivaakaviiva 4: pysty, oikea alakulma  8: alavaakaviiva 
//16: pysty, vasen alakulma (32) - ei sytytä mitään (desimaalipiste?) 64: pysty, oikea yläkulma, (128) - sytyttää kaksi, ei toimi
// -> näistä numerot: 
// 1: 4+64 = 68
// 2: 8+16+2+64=90 
// 3: 4+64+2+8=78
// 4: 1+2+64+4=71
// 5: 1+2+4+8=15
// 6: 1+16+8+4+2=31
// 7: 8+1+16 (nurinpäin) = 25
// 8: 64+16+8+4+2+1=95
// 9: 1+64+2+4+8=79  
// 0: 64+16+8+4+1=9

void setup() {
/* sarjamonitorin alustus */
  Serial.begin(9600);

/* timerkeskeytysten alustus */
  cli();
  //nollataan ensin rekisterit
  TCCR1A = 0;
  TCCR1B = 0;
  //asetetaan peruskeskeytys OCR1A vertailurekisteristä (WGM12 = 1)
  TCCR1B = 0b00001000;
  //näillä saadaan aikaan yhden sekunnin välein keskeytys
  //vertailuarvo 1 Hz (16MHz / 1024 / 1Hz - 1)
  OCR1A = 15624;
  TCCR1B |= 0b00000101; //asetetaan prescaler arvoon 1024 (CS10=1, CS12=1)
  TIMSK1 = 0b00000010; //asetetaan että vain A-keskeytys on sallittu (OCIE1A = 1)
  sei();  
/* satunnaislukugeneraattorin alustus kuten luennolla esitetty */
  int siemen = analogRead(A0);
  randomSeed(siemen);

/* ledipinnien alustus */
  pinMode(3, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);

/* painonappipinnien alustus */
  pinMode(8, INPUT_PULLUP);
  pinMode(9, INPUT_PULLUP);
  pinMode(10, INPUT_PULLUP);
  pinMode(11, INPUT_PULLUP);
  //Enabloi D8–D13 keskeytyspotentiaali
  PCICR = 0b00000001;
  //Salli painonappien aiheuttamat keskeytykset
  PCMSK0 = 0b00001111;

/* näyttöjen alustus */
  pinMode(dataPin,OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(latchPin, OUTPUT);

  delay(2000); //viivve että ehtii alustua

/* aloitusteksti sarjamonitorille ja näytön nollaus*/
  Serial.println("Hello! Peli alkaa!");
  updateScreen(0);
  delay(2000); // viive pelaajalle valmistautua
}

void loop() {
  // put your main code here, to run repeatedly:
//  Serial.println("Hello from Loop!");
// arvotaan ledi jos on timeri paukkunut
  if (timerkOn) {
    enableLed();
    timerkOn = false;
  }
  //tee nämä vain jos nappia on painettu
  if (nappiaPainettu) {
    uint8_t gameResult = checkGameResult(); //indikoi lopettamaan pelin jos mennyt väärin - palauttaa tuloksen
    updateScreen(gameResult); //päivittää näytölle tuloksen, kulkevan tai lopullisen
    nappiaPainettu = false; }
  //lopeta hallitusti viiveellä
  if (endGame) {
    delay(3000);
    exit(0);
  }
}

// Timer1 Compare A keskeytysrutiini
ISR(TIMER1_COMPA_vect) {
  timerkOn = true; //signaloi sytyttämään satunnainen ledi
}

//Keskeytyskäsittelijä kaikille painonapeille - hieman raskas ehkä?
ISR(PCINT0_vect) {
  //Debounce eli nappivärähtelyn huomioonotto, hyväksy vain yksi painallus järkevässä ajassa
  //500ms - Säädä tarvittaessa, tämä toimii
  if (!endGame) {
  unsigned long now = millis(); //luetaan nykyinen "aika-arvo"
  //käydään lävitse napit, pelissä vain yhtä nappia painetaan kerrallaan
  for (uint8_t i = 8; i < 12; i++) { 
    if ( (digitalRead(i) == LOW) && (now - lastPress > 500) ) {
      lastPress = now; //merkitään "edellinen validi keskeytysaika" 
      //debug tulostukset
      Serial.println("Nappia painettu: ");
      Serial.println(i);
      //skaalataan synkkaan ledi-indeksoinnin kanssa
      //huom! tässä vain tallennetaan nappienpainojärjestystä ja päivitetään nappilaskuri
      //myöhemmin otettava huomioon että jos viimeisin painallus olikin väärä
      i = i-8;
      buttonsSeq[buttonCount%100] = i; //rengaspuskuri
      buttonCount++;
      nappiaPainettu = true;
      return;
    } //if 
  } //for
  } //if
}

//saa nykyisen tuloksen parametriksi, tarkistaa vielä ettei päivitä turhaan
//tänne on tultava validi tieto tuloksesta! lopputulos ei enää päivity

void updateScreen(uint8_t luku) {
  static uint8_t tempLuku = 0xFF; //alustus varmistaa että alku-0 tulee ruudulle
  if (luku != tempLuku) { Serial.println("Tulos nyt: "); Serial.println(luku); tempLuku = luku;
  uint8_t kympit = luku / 10; //USB-virta ei riitä kahdelle näytölle,  muuten olisi tehtävissä
  uint8_t singlet = luku % 10; //näytetään vain ykköset siis

  digitalWrite(latchPin, LOW);
  // Ensin menisi kympit
  // shiftOut(dataPin, clockPin, MSBFIRST, numerotAsBitsToSerial[kympit]);
  // Sitten menee ykköset
  shiftOut(dataPin, clockPin, MSBFIRST, numerotAsBitsToSerial[singlet]);
  digitalWrite(latchPin, HIGH);
  }
}

void enableLed(){
// ledien hallitun vaihtumisen vuoksi ei hyväksytä peräkkäin kahta samaa lediä
// voisi toki toteuttaa myös niin että joka toisella timerilla olisi vain kaikkien sammutus
  static uint8_t randomNumber=0xFF;
//  Serial.println("Timer Interrupt Triggered!");
  uint8_t tempRandomNumber;
  while ( (tempRandomNumber = random(100)%4 ) == randomNumber) ; //arvo kunnes eri
  randomNumber=tempRandomNumber;  
//  Serial.println("Ledi:");
//  Serial.println(randomNumber);
  ledsSeq[ledCount%100] = randomNumber; //rengaspuskuri
//  Serial.println("ledCount:");
//  Serial.println(ledCount);
  ledCount++;
  //sytytetään haluttu ledi ja varmuudeksi sammutetaan kaikki muut
  switch (randomNumber) {
    case 0:
      digitalWrite(3, HIGH);
      digitalWrite(4, LOW);
      digitalWrite(5, LOW);
      digitalWrite(6, LOW);
      break;
    case 1:
      digitalWrite(4, HIGH);
      digitalWrite(3, LOW);
      digitalWrite(5, LOW);
      digitalWrite(6, LOW);
      break;
    case 2:
      digitalWrite(5, HIGH);
      digitalWrite(3, LOW);
      digitalWrite(4, LOW);
      digitalWrite(6, LOW);
      break;
    case 3:
      digitalWrite(6, HIGH);
      digitalWrite(3, LOW);
      digitalWrite(4, LOW);
      digitalWrite(5, LOW);
      break;
  }
 }

uint8_t checkGameResult(){
  uint8_t i = 0;
  //tarkistellaan rengaspuskurin rajoissa painettujen nappien osalta mätsäsikö ledeihin
  for (; i<buttonCount%100; i++) {
    if (buttonsSeq[i%100] != ledsSeq[i%100] ) {
      //cli();
      endGame = true;
      nappiaPainettu = false;
      Serial.println("Peli loppui");
      Serial.print("Painoit: ");
      Serial.println(buttonsSeq[i%100]);
      Serial.print("Vaikka olisi pitänyt painaa: ");
      Serial.println(ledsSeq[i%100]);
      Serial.print("Lopullinen tulos: ");
      Serial.println(buttonCount-1);
      delay(3000);
      return buttonCount-1; //vähennä väärä napinpainallus
    }
  } 
    return buttonCount; // palauttaa vielä jatkuvan pelin tämänhetkisen tuloksen
}