# Test Audiometrico con B-L475E-IOT01A (Versione Coerente al Firmware)

## 1) Obiettivo del progetto
Il progetto realizza un test audiometrico semplificato su scheda STM32 B-L475E-IOT01A.
Per ogni frequenza audio di interesse, il sistema genera un tono sinusoidale e aumenta gradualmente il livello fino a quando l'utente segnala la percezione tramite pulsante.
Il valore rilevato viene salvato come soglia uditiva relativa in dBFS.

Nel firmware attuale e implementato un solo ciclo di misura (un solo orecchio).

## 2) Frequenze testate
Le frequenze utilizzate sono:
- 125 Hz
- 250 Hz
- 500 Hz
- 1000 Hz
- 2000 Hz
- 4000 Hz
- 8000 Hz

Questi valori corrispondono alla banda tipica di un test audiometrico di base.

## 3) Architettura di generazione del segnale
La generazione del tono si basa su 4 blocchi:
- LUT sinusoidale (tabella campioni)
- Timer audio TIM4
- DMA (modalita circolare)
- DAC1 canale 1 su pin PA4

### 3.1 LUT
Il segnale sinusoidale e campionato in una tabella di 128 campioni (N_SAMPLES = 128), con valori a 12 bit nel range 0..4095.

Formula usata per ogni campione:

sample(i) = (sin(2*pi*i/N) + 1) * 2047.5

La LUT e costruita una sola volta all'avvio.

### 3.2 Timer audio e frequenza del tono
TIM4 genera eventi periodici TRGO (update event).
A ogni evento, il DMA invia un nuovo campione al DAC.

Con N campioni per periodo:

f_audio = f_timer_update / N

Nel firmware, il registro ARR di TIM4 viene aggiornato a ogni cambio frequenza per ottenere la f_audio desiderata.

### 3.3 DMA e double buffering
Il buffer DMA contiene due meta buffer (2*N campioni).
Quando il DMA completa una meta, la CPU aggiorna quella meta applicando il gain corrente.
In questo modo l'audio resta continuo senza glitch evidenti.

## 4) Controllo ampiezza (gain)
La LUT base e a piena scala. L'ampiezza effettiva e controllata da un gain lineare:

sample_out = sample_lut * gain

Nel firmware attuale:
- gain iniziale: 0.05
- incremento periodico: 0.02
- massimo: 1.0

Quindi il test parte a volume basso e aumenta nel tempo.

## 5) dBFS e significato del risultato
Il sistema salva il risultato in dBFS (non dBSPL assoluti), con:

dBFS = 20 * log10(gain)

Interpretazione:
- valore vicino a 0 dBFS: e servito livello alto per percepire il suono
- valore molto negativo: percezione avvenuta a livello basso

Se il pulsante non viene premuto entro il limite (gain arrivato a 1.0), la frequenza viene marcata come non percepita con valore simbolico -100 dBFS.

## 6) Macchina a stati del test (firmware attuale)
La logica e gestita da TIM2 con interrupt periodico (100 ms circa):

1. Riproduzione tono corrente
- TIM4 + DAC + DMA attivi sulla frequenza corrente

2. Attesa evento
- se utente preme pulsante (PC13): salva risultato e ferma tono
- altrimenti incrementa gain
- se gain raggiunge il massimo: salva non percepita e ferma tono

3. Pausa breve
- pausa silenziosa tra due frequenze (circa 1 s)

4. Avanzamento
- passa alla frequenza successiva
- reset gain al valore iniziale

5. Fine test
- dopo 7 frequenze, invio risultati su UART1 (115200 baud)

## 7) Configurazione periferiche coerente al progetto
- DAC1 CH1: PA4, trigger TIM4 TRGO, DMA circular
- TIM4: timer audio (scansione LUT)
- TIM2: timer controllo (100 ms)
- GPIO PC13: pulsante utente in EXTI falling
- USART1: TX PB6, RX PB7, 115200 baud

## 8) Limiti del prototipo
- Misura relativa in dBFS, non clinica in dBSPL
- Risultato dipendente da trasduttore audio, ambiente e calibrazione
- Implementazione corrente su un solo orecchio

## 9) Estensioni consigliate
- Secondo ciclo per orecchio sinistro/destro (due array risultati)
- Debounce software del pulsante
- Classificazione finale automatica (es. nella media / attenzione)
- Esportazione risultati in formato facilmente plottabile
- Stadio analogico di uscita dedicato per pilotaggio cuffie/cassa in modo corretto

## 10) Conclusione
Il firmware corrente realizza correttamente un audiometro didattico a soglia relativa:
- genera toni alle frequenze previste
- ricerca la soglia aumentando il gain
- salva i risultati in dBFS
- invia i dati via UART per analisi esterna

Questa versione e coerente con il codice implementato e pronta come base di relazione tecnica.