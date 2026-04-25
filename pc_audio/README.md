# Setup audio su PC (senza moduli analogici esterni)

## 1) Cosa fa questa modalita
- STM32 esegue tutta la logica del test audiometrico.
- STM32 invia comandi su UART con indicazione dell'orecchio (`AUDIO START L/R`, `AUDIO GAIN L/R`, `AUDIO STOP`).
- Script Python su PC genera il suono con la scheda audio del computer.

## 2) Prerequisiti
- Python 3 installato su PC.
- Scheda STM32 collegata via USB e firmware caricato.
- Cuffie o casse collegate al PC.

## 3) Creazione ambiente virtuale (consigliato)
Da terminale PowerShell nella root del progetto:

```powershell
py -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
```

Nota: se lo script di attivazione e bloccato da policy PowerShell, esegui una volta:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

## 4) Installazione dipendenze Python
Da terminale nella cartella progetto:

```bash
pip install -r pc_audio/requirements.txt
```

## 5) Trovare la porta seriale
In Windows controlla in Gestione dispositivi la porta COM della scheda (esempio `COM8`).

## 6) Avvio script

```bash
python pc_audio/pc_audio_player.py --port COM8 --baud 115200 --master-gain 0.10
```

Sostituisci `COM8` con la tua porta reale.

Il parametro `--master-gain` attenua globalmente il volume lato PC.
Valori utili tipici: `0.05` (molto basso), `0.10` (consigliato), `0.20` (piu alto).

Il test viene eseguito prima sull'orecchio sinistro e poi sul destro.

## 7) Uso durante il test
- Avvia lo script Python.
- Avvia il firmware STM32.
- Lo script riproduce i toni quando riceve i comandi e li panora sul canale sinistro o destro secondo l'orecchio indicato.
- Premi il pulsante su scheda quando senti il tono.

## 8) Troubleshooting rapido
- Nessun suono: verifica volume output PC e dispositivo audio predefinito.
- Errore seriale: controlla la COM corretta e che nessun altro programma la stia usando.
- Distorsione: abbassa il volume di sistema del PC.
