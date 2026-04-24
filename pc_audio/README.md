# Setup audio su PC (senza moduli analogici esterni)

## 1) Cosa fa questa modalita
- STM32 esegue tutta la logica del test audiometrico.
- STM32 invia comandi su UART (`AUDIO START`, `AUDIO GAIN`, `AUDIO STOP`).
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
python pc_audio/pc_audio_player.py --port COM8 --baud 115200
```

Sostituisci `COM8` con la tua porta reale.

## 7) Uso durante il test
- Avvia lo script Python.
- Avvia il firmware STM32.
- Lo script riproduce i toni quando riceve i comandi.
- Premi il pulsante su scheda quando senti il tono.

## 8) Troubleshooting rapido
- Nessun suono: verifica volume output PC e dispositivo audio predefinito.
- Errore seriale: controlla la COM corretta e che nessun altro programma la stia usando.
- Distorsione: abbassa il volume di sistema del PC.
