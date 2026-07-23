/*
 * ================================================================
 * MINI-ASCENSEUR R+2 - GROUPE 9
 * LCD AMÉLIORÉ - SUIVI COMPLET DE L'ASCENSEUR
 * ================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================================================================
// 1. CONFIGURATION MATÉRIELLE
// ================================================================

// --- Moteur (L298N - CANAL B) ---
#define MOTOR_ENB    27
#define MOTOR_IN3    14
#define MOTOR_IN4    13

// --- PARAMÈTRES DE CADENÇAGE ---
const unsigned long DUREE_IMPULSION_ON_MONTEE = 100;
const unsigned long DUREE_IMPULSION_OFF_MONTEE = 1300;
const unsigned long DUREE_IMPULSION_ON_DESCENTE = 110;
const unsigned long DUREE_IMPULSION_OFF_DESCENTE = 1000;

// --- Vitesses initiales (multiples de 5) ---
const int VITESSE_MONTEE_INITIALE = 185;
const int VITESSE_DESCENTE_INITIALE = 135;
int VITESSE_MONTEE = VITESSE_MONTEE_INITIALE;
int VITESSE_DESCENTE = VITESSE_DESCENTE_INITIALE;
const int VITESSE_MAX = 255;
const unsigned long DELAI_BLOCAGE = 10000;

// --- Capteurs IR ---
#define IR0      16
#define IR1_IN   17
#define IR1_END  18
#define IR2      19

const int IR_PINS_IN[3] = {IR0, IR1_IN, IR2};
const int IR_PINS_END[3] = {-1, IR1_END, -1};

// --- Boutons d'appel ---
const int BTN_0_1 = 33;
const int BTN_0_2 = 25;
const int BTN_1_2 = 26;
const int BTN_1_0 = 4;
const int BTN_2_0 = 5;
const int BTN_2_1 = 32;

const int BTN_PINS[6] = {BTN_0_1, BTN_0_2, BTN_1_2, BTN_1_0, BTN_2_0, BTN_2_1};
const int BTN_ORIGINE[6] = {0, 0, 1, 1, 2, 2};
const int BTN_DEST[6] = {1, 2, 2, 0, 0, 1};
const char* BTN_NOMS[6] = {"BTN_0_1", "BTN_0_2", "BTN_1_2", "BTN_1_0", "BTN_2_0", "BTN_2_1"};

// --- Bouton d'urgence physique ---
#define BTN_URGENCE 15

// --- LCD ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================================================================
// 2. PARAMÈTRES
// ================================================================

const unsigned long CAPTEUR_DEBOUNCE = 150;
const unsigned long BTN_DEBOUNCE = 100;
const unsigned long CONFIRMATION_DELAI = 300;
const unsigned long URGENCE_DEBOUNCE = 200;

// ================================================================
// 3. VARIABLES
// ================================================================

enum EtatSysteme {INIT, ARRET, MONTEE, DESCENTE, URGENCE};
EtatSysteme etat = INIT;

int etageCourant = 0;
bool destinations[3] = {false};

bool capteurInActif[3] = {false};
bool capteurEndActif[3] = {false};
unsigned long lastInTime[3] = {0};
unsigned long lastEndTime[3] = {0};

unsigned long tempsConfirmation[3] = {0};
bool confirmationEnCours[3] = {false};
bool etageConfirme[3] = {false};

int direction = 0;
bool approcheActive = false;
unsigned long debutApproche = 0;
int etageCible = -1;
unsigned long tempsDebutMouvement = 0;
bool mouvementEnCours = false;

unsigned long lastDebounce[6] = {0};
bool homingTermine = false;
bool urgenceActive = false;
unsigned long lastUrgenceTime = 0;

// CADENÇAGE
bool impulsionEnCours = false;
unsigned long debutImpulsion = 0;
int vitesseImpulsion = 0;
bool sensImpulsion = true;
unsigned long dureeOn = 0;
unsigned long dureeOff = 0;

// ================================================================
// 4. PROTOTYPES
// ================================================================

void monter();
void descendre();
void stopMoteur();
void setVitesse(int vitesse);
void homingSequence();
void lireBoutons();
void lireCapteursIR();
void verifierArret(int etageDetecte);
void gererArret();
int trouverProchainAppel();
bool estAEtage(int etage);
void afficherEtatDestinations();
void updateLCD();
void checkUrgenceClavier();
void checkUrgencePhysique();
void arretUrgence();
void reinitialiserApresUrgence();
void demarrerImpulsion(int vitesse, bool montee);
void gererImpulsion();
void augmenterVitesse();
void afficherEtatComplet();
void reinitialiserVitesses();

// ================================================================
// 5. SETUP
// ================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("========================================");
    Serial.println(" MINI-ASCENSEUR R+2 - GROUPE 9");
    Serial.println(" LCD AMELIORE - SUIVI COMPLET");
    Serial.println("========================================");
    Serial.println();

    // --- Initialisation LCD ---
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Ascenseur R+2");
    lcd.setCursor(0, 1);
    lcd.print("Groupe 9 - Init");
    delay(1500);

    // --- Moteur ---
    pinMode(MOTOR_IN3, OUTPUT);
    pinMode(MOTOR_IN4, OUTPUT);
    digitalWrite(MOTOR_IN3, LOW);
    digitalWrite(MOTOR_IN4, LOW);

    ledcSetup(0, 1000, 8);
    ledcAttachPin(MOTOR_ENB, 0);
    ledcWrite(0, 0);

    // --- Capteurs IR ---
    pinMode(IR0, INPUT_PULLUP);
    pinMode(IR1_IN, INPUT_PULLUP);
    pinMode(IR1_END, INPUT_PULLUP);
    pinMode(IR2, INPUT_PULLUP);

    // --- Boutons ---
    for (int i = 0; i < 6; i++) {
        pinMode(BTN_PINS[i], INPUT_PULLUP);
    }

    // --- Bouton d'urgence ---
    pinMode(BTN_URGENCE, INPUT_PULLUP);

    // --- Homing ---
    delay(500);
    homingSequence();
    
    while (!homingTermine) {
        delay(100);
    }
    
    etat = ARRET;
    direction = 0;
    updateLCD();

    Serial.println("\n>>> ✅ SYSTEME PRET <<<");
    Serial.println("⚠️ URGENCE : ESPACE (clavier) ou bouton physique (GPIO 15)");
    Serial.println();
}

// ================================================================
// 6. RÉINITIALISATION DES VITESSES
// ================================================================

void reinitialiserVitesses() {
    VITESSE_MONTEE = VITESSE_MONTEE_INITIALE;
    VITESSE_DESCENTE = VITESSE_DESCENTE_INITIALE;
}

// ================================================================
// 7. LOOP
// ================================================================

void loop() {
    checkUrgenceClavier();
    checkUrgencePhysique();
    
    if (urgenceActive) {
        delay(50);
        return;
    }

    lireBoutons();
    lireCapteursIR();

    if (etat == MONTEE || etat == DESCENTE) {
        gererImpulsion();
        
        if (mouvementEnCours) {
            if (millis() - tempsDebutMouvement > DELAI_BLOCAGE) {
                augmenterVitesse();
                tempsDebutMouvement = millis();
            }
        }
    }

    if (approcheActive) {
        if (millis() - debutApproche > 50) {
            impulsionEnCours = false;
            ledcWrite(0, 0);
            digitalWrite(MOTOR_IN3, LOW);
            digitalWrite(MOTOR_IN4, LOW);
            
            approcheActive = false;
            mouvementEnCours = false;
            
            if (etageCible >= 0 && etageCible < 3) {
                destinations[etageCible] = false;
                etageConfirme[etageCible] = false;
                Serial.println("[CALAGE] ✅ Etage " + String(etageCible) + " atteint");
                afficherEtatDestinations();
            }
            
            delay(300);
            
            // Affichage LCD : Porte ouverte
            lcd.setCursor(0, 1);
            lcd.print("Porte ouverte   ");
            delay(1500);
            lcd.setCursor(0, 1);
            lcd.print("Porte fermee    ");
            
            etageCible = -1;
            etat = ARRET;
            direction = 0;
            
            reinitialiserVitesses();
            
            int prochain = trouverProchainAppel();
            if (prochain != -1 && prochain != etageCourant) {
                if (prochain > etageCourant) {
                    monter();
                } else if (prochain < etageCourant) {
                    descendre();
                }
            }
        }
    }

    if (etat == ARRET) {
        gererArret();
    }

    updateLCD();
    delay(20);
}

// ================================================================
// 8. AUGMENTATION DE VITESSE
// ================================================================

void augmenterVitesse() {
    if (etat == MONTEE) {
        if (VITESSE_MONTEE < VITESSE_MAX) {
            VITESSE_MONTEE += 5;
            Serial.println("[VITESSE] ⬆️ Montee augmentee a " + String(VITESSE_MONTEE));
            // Affichage LCD
            lcd.setCursor(0, 1);
            lcd.print("Vitesse+ " + String(VITESSE_MONTEE));
        }
    } else if (etat == DESCENTE) {
        if (VITESSE_DESCENTE < VITESSE_MAX) {
            VITESSE_DESCENTE += 5;
            Serial.println("[VITESSE] ⬆️ Descente augmentee a " + String(VITESSE_DESCENTE));
            // Affichage LCD
            lcd.setCursor(0, 1);
            lcd.print("Vitesse+ " + String(VITESSE_DESCENTE));
        }
    }
}

// ================================================================
// 9. CADENÇAGE
// ================================================================

void demarrerImpulsion(int vitesse, bool montee) {
    vitesseImpulsion = vitesse;
    sensImpulsion = montee;
    impulsionEnCours = false;
    debutImpulsion = 0;
    mouvementEnCours = true;
    tempsDebutMouvement = millis();
    
    if (montee) {
        etat = MONTEE;
        direction = 1;
        dureeOn = DUREE_IMPULSION_ON_MONTEE;
        dureeOff = DUREE_IMPULSION_OFF_MONTEE;
        Serial.println("[ACTION] ⬆️ MONTEE (vitesse " + String(vitesse) + ")");
        lcd.setCursor(0, 1);
        lcd.print("⬆️ Mont�e        ");
    } else {
        etat = DESCENTE;
        direction = -1;
        dureeOn = DUREE_IMPULSION_ON_DESCENTE;
        dureeOff = DUREE_IMPULSION_OFF_DESCENTE;
        Serial.println("[ACTION] ⬇️ DESCENTE (vitesse " + String(vitesse) + ")");
        lcd.setCursor(0, 1);
        lcd.print("⬇️ Descente      ");
    }
}

void gererImpulsion() {
    if (!impulsionEnCours) {
        impulsionEnCours = true;
        debutImpulsion = millis();
        
        if (sensImpulsion) {
            digitalWrite(MOTOR_IN3, HIGH);
            digitalWrite(MOTOR_IN4, LOW);
        } else {
            digitalWrite(MOTOR_IN3, LOW);
            digitalWrite(MOTOR_IN4, HIGH);
        }
        ledcWrite(0, vitesseImpulsion);
    } else {
        if (millis() - debutImpulsion >= dureeOn) {
            ledcWrite(0, 0);
            digitalWrite(MOTOR_IN3, LOW);
            digitalWrite(MOTOR_IN4, LOW);
            impulsionEnCours = false;
            debutImpulsion = millis();
        }
    }
}

// ================================================================
// 10. URGENCE
// ================================================================

void checkUrgenceClavier() {
    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd == ' ') {
            if (!urgenceActive) {
                arretUrgence();
            } else {
                reinitialiserApresUrgence();
            }
        }
    }
}

void checkUrgencePhysique() {
    static bool dernierEtatBouton = HIGH;
    static unsigned long dernierChangement = 0;
    
    bool etatBouton = digitalRead(BTN_URGENCE);
    
    if (etatBouton == LOW && dernierEtatBouton == HIGH) {
        if (millis() - dernierChangement > URGENCE_DEBOUNCE) {
            dernierChangement = millis();
            
            if (digitalRead(BTN_URGENCE) == LOW) {
                if (!urgenceActive) {
                    arretUrgence();
                } else {
                    reinitialiserApresUrgence();
                }
            }
        }
    }
    
    dernierEtatBouton = etatBouton;
}

void arretUrgence() {
    urgenceActive = true;
    ledcWrite(0, 0);
    digitalWrite(MOTOR_IN3, LOW);
    digitalWrite(MOTOR_IN4, LOW);
    impulsionEnCours = false;
    mouvementEnCours = false;
    etat = URGENCE;
    direction = 0;
    approcheActive = false;
    
    Serial.println("🚨 ARRET D'URGENCE ACTIF");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("🚨 URGENCE !");
    lcd.setCursor(0, 1);
    lcd.print("Appuyez reset");
}

void reinitialiserApresUrgence() {
    urgenceActive = false;
    
    for (int i = 0; i < 3; i++) {
        destinations[i] = false;
        capteurInActif[i] = false;
        capteurEndActif[i] = false;
        confirmationEnCours[i] = false;
        etageConfirme[i] = false;
    }
    impulsionEnCours = false;
    mouvementEnCours = false;
    
    reinitialiserVitesses();
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Reinitialisation");
    lcd.setCursor(0, 1);
    lcd.print("...");
    delay(500);
    
    homingTermine = false;
    homingSequence();
    
    while (!homingTermine) {
        delay(100);
    }
    
    etat = ARRET;
    direction = 0;
    updateLCD();
}

// ================================================================
// 11. MOTEUR
// ================================================================

void setVitesse(int vitesse) {
    ledcWrite(0, vitesse);
}

void monter() {
    demarrerImpulsion(VITESSE_MONTEE, true);
}

void descendre() {
    demarrerImpulsion(VITESSE_DESCENTE, false);
}

void stopMoteur() {
    ledcWrite(0, 0);
    digitalWrite(MOTOR_IN3, LOW);
    digitalWrite(MOTOR_IN4, LOW);
    impulsionEnCours = false;
    mouvementEnCours = false;
    etat = ARRET;
    direction = 0;
    Serial.println("[ACTION] ⏹️ ARRET");
}

// ================================================================
// 12. HOMING
// ================================================================

void homingSequence() {
    Serial.println("[HOMING] Retour a l'etage 0...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Homing...");
    lcd.setCursor(0, 1);
    lcd.print("-> Etage 0");
    homingTermine = false;

    if (estAEtage(0)) {
        etageCourant = 0;
        Serial.println("[HOMING] Deja a l'etage 0");
        homingTermine = true;
        return;
    }

    destinations[0] = true;
    descendre();
    
    while (!estAEtage(0) && !urgenceActive) {
        gererImpulsion();
        checkUrgenceClavier();
        checkUrgencePhysique();
        if (urgenceActive) {
            stopMoteur();
            return;
        }
        delay(5);
    }
    
    stopMoteur();
    delay(300);
    
    etageCourant = 0;
    destinations[0] = false;
    homingTermine = true;
    
    Serial.println("[HOMING] ✅ Etage 0 atteint");
    lcd.setCursor(0, 1);
    lcd.print("Etage 0 OK !");
    delay(500);
}

// ================================================================
// 13. POSITION
// ================================================================

bool estAEtage(int etage) {
    if (etage == 0) {
        return (digitalRead(IR0) == LOW);
    } else if (etage == 1) {
        return (digitalRead(IR1_IN) == LOW && digitalRead(IR1_END) == LOW);
    } else if (etage == 2) {
        return (digitalRead(IR2) == LOW);
    }
    return false;
}

// ================================================================
// 14. BOUTONS
// ================================================================

void lireBoutons() {
    for (int i = 0; i < 6; i++) {
        if (digitalRead(BTN_PINS[i]) == LOW) {
            if (millis() - lastDebounce[i] > BTN_DEBOUNCE) {
                lastDebounce[i] = millis();
                
                if (digitalRead(BTN_PINS[i]) == LOW) {
                    int dest = BTN_DEST[i];
                    int origine = BTN_ORIGINE[i];
                    
                    Serial.println("[APPEL] 🔔 " + String(BTN_NOMS[i]) + 
                                 " : Etage " + String(origine) + " → Etage " + String(dest));
                    
                    if (!destinations[dest]) {
                        destinations[dest] = true;
                        afficherEtatDestinations();
                        reinitialiserVitesses();
                    }
                    
                    if (etat == ARRET) {
                        gererArret();
                    }
                }
            }
        }
    }
}

void afficherEtatDestinations() {
    String msg = "[FILE] Destinations: ";
    bool hasDest = false;
    for (int i = 0; i < 3; i++) {
        if (destinations[i]) {
            msg += String(i) + " ";
            hasDest = true;
        }
    }
    if (!hasDest) msg += "aucune";
    Serial.println(msg);
}

void afficherEtatComplet() {
    Serial.println("========================================");
    Serial.println("📊 ETAT COMPLET DU SYSTEME");
    Serial.println("   Etat          : " + String(etat));
    Serial.println("   Etage courant : " + String(etageCourant));
    Serial.println("   Direction     : " + String(direction));
    Serial.print("   Destinations  : ");
    for (int i = 0; i < 3; i++) {
        if (destinations[i]) Serial.print(String(i) + " ");
    }
    if (!destinations[0] && !destinations[1] && !destinations[2]) Serial.print("aucune");
    Serial.println();
    Serial.println("   Vitesse montee   : " + String(VITESSE_MONTEE));
    Serial.println("   Vitesse descente : " + String(VITESSE_DESCENTE));
    Serial.println("========================================");
}

// ================================================================
// 15. CAPTEURS IR
// ================================================================

void lireCapteursIR() {
    for (int etage = 0; etage < 3; etage++) {
        bool inActif = (digitalRead(IR_PINS_IN[etage]) == LOW);
        bool endActif = false;
        
        if (etage == 0) {
            endActif = inActif;
        } else if (etage == 1) {
            endActif = (digitalRead(IR_PINS_END[etage]) == LOW);
        } else if (etage == 2) {
            endActif = inActif;
        }
        
        if (inActif && !capteurInActif[etage]) {
            if (millis() - lastInTime[etage] > CAPTEUR_DEBOUNCE) {
                capteurInActif[etage] = true;
                lastInTime[etage] = millis();
                if (etage == 1) {
                    Serial.println("[DETECTION] 📍 Entree etage 1");
                    if (destinations[1] && direction == -1) {
                        verifierArret(1);
                    }
                }
                if (etage == 2) {
                    Serial.println("[DETECTION] 📍 Etage 2");
                    if (destinations[2]) {
                        verifierArret(2);
                    }
                }
                if (etage == 0) {
                    Serial.println("[DETECTION] 📍 Etage 0");
                    if (destinations[0]) {
                        verifierArret(0);
                    }
                }
            }
        } else if (!inActif && capteurInActif[etage]) {
            capteurInActif[etage] = false;
            lastInTime[etage] = millis();
        }
        
        if (etage == 1) {
            if (endActif && !capteurEndActif[etage]) {
                if (millis() - lastEndTime[etage] > CAPTEUR_DEBOUNCE) {
                    capteurEndActif[etage] = true;
                    lastEndTime[etage] = millis();
                    Serial.println("[DETECTION] 📍 Fin etage 1");
                    if (destinations[1] && direction == 1) {
                        verifierArret(1);
                    }
                }
            } else if (!endActif && capteurEndActif[etage]) {
                capteurEndActif[etage] = false;
                lastEndTime[etage] = millis();
            }
        }
        
        bool aLEtage = false;
        if (etage == 0) {
            aLEtage = inActif;
        } else if (etage == 1) {
            aLEtage = (inActif && endActif);
        } else if (etage == 2) {
            aLEtage = inActif;
        }
        
        if (aLEtage) {
            if (!confirmationEnCours[etage]) {
                confirmationEnCours[etage] = true;
                tempsConfirmation[etage] = millis();
            } else if (millis() - tempsConfirmation[etage] > CONFIRMATION_DELAI) {
                if (!etageConfirme[etage]) {
                    etageConfirme[etage] = true;
                    Serial.println("[POSITION] ✅ Etage " + String(etage) + " confirme");
                }
                
                if (etageCourant != etage) {
                    etageCourant = etage;
                    Serial.println("[POSITION] 📍 Cabine a l'etage " + String(etage));
                }
            }
        } else {
            if (confirmationEnCours[etage]) {
                confirmationEnCours[etage] = false;
            }
            if (etageConfirme[etage]) {
                etageConfirme[etage] = false;
                Serial.println("[POSITION] ❌ Cabine quitte l'etage " + String(etage));
            }
        }
    }
}

// ================================================================
// 16. LOGIQUE
// ================================================================

void verifierArret(int etageDetecte) {
    if (etat == ARRET || etat == URGENCE) return;
    
    if (destinations[etageDetecte]) {
        Serial.println("[APPROCHE] 🛑 Arret demande a l'etage " + String(etageDetecte));
        
        stopMoteur();
        
        etageCourant = etageDetecte;
        destinations[etageDetecte] = false;
        etageConfirme[etageDetecte] = false;
        
        Serial.println("[CALAGE] ✅ Etage " + String(etageDetecte) + " atteint");
        afficherEtatDestinations();
        afficherEtatComplet();
        
        reinitialiserVitesses();
        
        int prochain = trouverProchainAppel();
        if (prochain != -1 && prochain != etageCourant) {
            delay(500);
            if (prochain > etageCourant) {
                monter();
            } else if (prochain < etageCourant) {
                descendre();
            }
        }
    }
}

void gererArret() {
    int prochain = trouverProchainAppel();
    if (prochain == -1) return;
    
    if (prochain > etageCourant) {
        monter();
    } else if (prochain < etageCourant) {
        descendre();
    } else {
        destinations[etageCourant] = false;
        Serial.println("[INFO] Deja a l'etage " + String(etageCourant));
        afficherEtatDestinations();
        lcd.setCursor(0, 1);
        lcd.print("Deja present    ");
        delay(1500);
        
        int prochain2 = trouverProchainAppel();
        if (prochain2 != -1 && prochain2 != etageCourant) {
            if (prochain2 > etageCourant) monter();
            else if (prochain2 < etageCourant) descendre();
        }
    }
}

int trouverProchainAppel() {
    if (direction == 1) {
        for (int i = etageCourant + 1; i < 3; i++) {
            if (destinations[i]) return i;
        }
        for (int i = etageCourant - 1; i >= 0; i--) {
            if (destinations[i]) return i;
        }
    } else if (direction == -1) {
        for (int i = etageCourant - 1; i >= 0; i--) {
            if (destinations[i]) return i;
        }
        for (int i = etageCourant + 1; i < 3; i++) {
            if (destinations[i]) return i;
        }
    }
    
    int distMin = 999;
    int plusProche = -1;
    for (int i = 0; i < 3; i++) {
        if (destinations[i]) {
            int dist = abs(i - etageCourant);
            if (dist < distMin) {
                distMin = dist;
                plusProche = i;
            }
        }
    }
    if (destinations[etageCourant]) return etageCourant;
    return plusProche;
}

// ================================================================
// 17. LCD - AFFICHAGE PRINCIPAL
// ================================================================

void updateLCD() {
    if (urgenceActive) {
        lcd.setCursor(0, 0);
        lcd.print("🚨 URGENCE !");
        lcd.setCursor(0, 1);
        lcd.print("Appuyez reset");
        return;
    }
    
    // ---- LIGNE 0 : Étage + Direction ----
    lcd.setCursor(0, 0);
    
    // Afficher l'étage avec une icône
    lcd.print("Et:");
    lcd.print(etageCourant);
    lcd.print(" ");
    
    // Afficher la direction
    if (etat == MONTEE) {
        lcd.print("⬆ MONTEE");
    } else if (etat == DESCENTE) {
        lcd.print("⬇ DESCEND");
    } else {
        lcd.print("⏹ ARRET ");
    }
    
    // ---- LIGNE 1 : Destinations ----
    lcd.setCursor(0, 1);
    bool hasDest = false;
    String file = "";
    for (int i = 0; i < 3; i++) {
        if (destinations[i]) {
            if (file.length() > 0) file += " ";
            file += String(i);
            hasDest = true;
        }
    }
    
    if (hasDest) {
        // Afficher les destinations
        String ligne = "Dest:" + file;
        // Ajouter des espaces pour effacer l'ancien texte
        while (ligne.length() < 16) ligne += " ";
        lcd.print(ligne);
    } else {
        // Aucune destination
        if (etat == ARRET) {
            lcd.print("Aucune demande  ");
        } else {
            lcd.print("En cours...     ");
        }
    }
}