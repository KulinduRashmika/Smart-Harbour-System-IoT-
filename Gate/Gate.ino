#define MQ2_A0 A0

void setup() {
  Serial.begin(115200);
  Serial.println("MQ-2 Multi-Gas Approximation");
}

void loop() {
  int gasLevel = analogRead(MQ2_A0);

  Serial.print("Gas Level: ");
  Serial.print(gasLevel);
  Serial.print("  ");

  if (gasLevel < 20) {
    Serial.println("Clean Air");
  } 
  else if (gasLevel < 30) {
    Serial.println("Possible Alcohol/Smoke");
  } 
  else if (gasLevel < 50) {
    Serial.println("Possible LPG or Methane");
  } 
  else {
    Serial.println("High Gas Concentration!");
  }

  delay(1000); // wait 1 second before next reading
}