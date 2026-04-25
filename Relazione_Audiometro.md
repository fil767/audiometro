# Test Audiometrico con B-L475E-IOT01A (Versione Coerente al Firmware Attuale)

## 1) Obiettivo del progetto
Il progetto realizza un test audiometrico didattico a soglia relativa.
La scheda STM32 gestisce la logica del test (sequenza frequenze, temporizzazione, pulsante, salvataggio risultati), mentre la riproduzione audio avviene sul PC tramite script Python collegato su UART.

Il risultato e una soglia per frequenza espressa in dBFS (non dB HL clinici).

## 2) Frequenze testate
Le frequenze attualmente usate sono:
- 125 Hz
- 250 Hz
- 500 Hz
- 750 Hz
- 1000 Hz
- 1500 Hz
- 2000 Hz
- 3000 Hz
- 4000 Hz
- 6000 Hz
- 8000 Hz

Questa griglia e piu fitta rispetto alla versione iniziale e migliora la lettura della curva soglia.

## 3) Architettura del sistema
### 3.1 Ruolo della scheda STM32
La scheda:
- definisce frequenza corrente e livello corrente
- gestisce il timer di controllo
- legge il pulsante utente (PC13)
- salva il valore soglia rilevato
- invia comandi seriali al PC

### 3.2 Ruolo del PC (script Python)
Lo script Python:
- riceve i comandi seriali `AUDIO START`, `AUDIO GAIN`, `AUDIO STOP`, `AUDIO DONE`
- genera il tono alla frequenza richiesta
- aggiorna il volume in base al gain ricevuto

In questa configurazione non e necessario uno stadio analogico esterno per cuffie.

## 4) Controllo del livello (dBFS)
Il firmware lavora con una rampa in dBFS e converte in gain lineare per l'audio:

gain = 10^(dBFS/20)

Parametri attuali:
- livello iniziale: -70 dBFS
- incremento: +1 dB ogni tick di TIM2
- limite massimo: -20 dBFS

Se l'utente non preme entro il limite, la frequenza e marcata come non percepita (`-100 dBFS` simbolico).

## 5) Temporizzazioni del test
TIM2 e configurato con tick di circa 500 ms.

Con questa scelta:
- il livello cresce lentamente (1 dB ogni 500 ms)
- la sensibilita al tempo di reazione dell'utente e ridotta

Tra due frequenze e prevista una pausa di circa 2 s.

## 6) Macchina a stati (flusso operativo)
1. Avvio frequenza corrente
- invio `AUDIO START freq gain`

2. Attesa risposta
- se pulsante premuto: salva soglia corrente in dBFS
- se non premuto: aumenta livello e invia `AUDIO GAIN`

3. Caso limite
- se raggiunge -5 dBFS senza risposta: salva non percepita

4. Pausa e avanzamento
- invio `AUDIO STOP`
- pausa di 2 s
- frequenza successiva

5. Fine test
- invio `AUDIO DONE`
- stampa risultati su UART

## 7) Configurazione periferiche rilevanti
- TIM2: timer di controllo (500 ms)
- GPIO PC13: pulsante utente in EXTI falling
- USART1: 115200 baud (comunicazione con script PC)

Nota: il progetto mantiene anche la struttura DAC/TIM4/DMA nel codice, ma la modalita operativa attuale e PC audio (`USE_PC_AUDIO = 1`).

## 8) Calibrazione didattica (procedura adottata)
Poiche non e presente calibrazione metrologica in dB HL, si adotta una calibrazione didattica per rendere i risultati ripetibili.

### 8.1 Scopo
Ottenere misure confrontabili nel tempo sullo stesso setup, senza pretesa clinica.

### 8.2 Condizioni fisse da mantenere
- stesse cuffie
- stesso PC/dispositivo audio
- stessi parametri firmware
- stesso volume di sistema
- ambiente il piu possibile silenzioso
- effetti audio software disattivati (EQ, enhancement, loudness)

### 8.3 Procedura pratica
1. Impostare il volume PC a un valore fisso (es. 25-35%).
2. Eseguire un test di prova completo.
3. Se molte frequenze risultano non percepite, aumentare di poco il volume (+5%).
4. Se i toni risultano subito troppo forti, diminuire di poco il volume (-5%).
5. Bloccare il volume scelto e non modificarlo piu durante le sessioni.
6. Registrare questo setup come riferimento (profilo baseline).

### 8.4 Interpretazione dei risultati
I valori ottenuti sono relativi al setup scelto.
Sono utili per confronto interno (sessione vs sessione), non per diagnosi clinica.

## 9) Limiti del prototipo
- misura relativa in dBFS, non clinica in dB HL/dBSPL
- dipendenza da cuffie, volume PC e ambiente
- un solo orecchio nella versione attuale

## 10) Estensioni consigliate
- doppio ciclo orecchio sinistro/destro
- protocollo soglia tipo Hughson-Westlake (down 10 / up 5)
- esportazione CSV e grafico automatico lato PC
- debounce pulsante e controlli di affidabilita risposta

## 11) Conclusione
Il sistema attuale realizza correttamente un audiometro didattico con riproduzione su PC:
- la scheda STM32 governa l'intera logica del test
- il PC genera il suono in base ai comandi seriali
- le soglie vengono salvate in dBFS e confrontate in modo ripetibile tramite calibrazione didattica.