#!/bin/bash
# Script to start API with virtual environment
# Run with: bash start_api.sh

# Check if venv exists
if [ ! -d "venv" ]; then
    echo "[ERROR] Virtual environment does not exist!"
    echo "[INFO] Run first: bash setup_pi.sh"
    exit 1
fi

# Check if model exists
if [ ! -f "udare_model.tflite" ]; then
    echo "[ERROR] TFLite model does not exist!"
    echo "[INFO] Train on laptop: python train.py"
    echo "[INFO] Transfer udare_model.tflite and scaler.pkl to Pi"
    exit 1
fi

# Activate virtual environment and start API
echo "[START] Starting Automated Watering System API..."
echo "[INFO] Location: http://$(hostname -I | awk '{print $1}'):5000"
echo "[INFO] Stop with: Ctrl+C"
echo ""

source venv/bin/activate
python app.py
