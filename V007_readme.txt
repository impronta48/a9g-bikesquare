Versione 007 

GPIO_06 -> Locker Command Input A
GPIO_07 -> Locker Command Input B

Logica 
I piedini GPIO_06 e GPIO_07 al boot partono con livello basso.
Al momento sono pilotati sia dalla ricezione di un comando motore on/off (per provarlo subit0) che dal futuro comando lock/unlock.

alla ricezione del comando [on] si dovrebbe sbloccare il lucchetto:
- il piedino GPIO_07 (B) passa per un secondo allo stato HIGH e poi torna a LOW

alla ricezione del comando [off] si dovrebbe bloccare il lucchetto:
- il piedino GPIO_06 (A) passa per un secondo allo stato HIGH e poi torna a LOW

NOTE:

sulla mia bici:
 - non riesco a pilotare l'ON/OFF dal sito 
 - mi arriva sempre [on] ad ogni risposta e quindi si traduce in un lampeggio del piedino 7. 
   su questo ultimo punto posso mettere un flag per evitare di ribadire il comando se non cambia. 
   Vediamo prima come si comporta con le bici di Massimo e con il lucchetto.






