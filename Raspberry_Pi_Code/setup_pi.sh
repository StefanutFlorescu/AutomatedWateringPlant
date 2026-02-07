#!/bin/bash
# Installation script for Raspberry Pi Zero
# Run with: bash setup_pi.sh

set -e  # Stop on first error

echo "========================================================"
echo "  Setup Automated Watering System - Raspberry Pi Zero  "
echo "========================================================"
echo ""

# Check if we are on Raspberry Pi
if [ ! -f /etc/rpi-issue ]; then
    echo "[WARNING] This does not appear to be a Raspberry Pi"
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# 1. Install system dependencies
echo "[1/5] Installing system dependencies..."
sudo apt-get update
sudo apt-get install -y python3-full python3-pip python3-venv

# 2. Create virtual environment
echo ""
echo "[2/5] Creating virtual environment..."
if [ -d "venv" ]; then
    echo "   Virtual environment already exists, removing..."
    rm -rf venv
fi
python3 -m venv venv

# 3. Activate venv and install packages
echo ""
echo "[3/5] Installing Python packages..."
source venv/bin/activate

# Upgrade pip
pip install --upgrade pip

# Try to install tflite-runtime
echo ""
echo "   Installing TensorFlow Lite runtime..."
if pip install --extra-index-url https://google-coral.github.io/py-repo/ tflite-runtime; then
    echo "   [OK] tflite-runtime installed successfully"
else
    echo "   [WARNING] tflite-runtime not available, installing tensorflow (heavier)..."
    pip install tensorflow==2.13.0
fi

# Install remaining dependencies
echo ""
echo "   Installing Flask and dependencies..."
pip install Flask==2.3.3 Werkzeug==2.3.7
pip install numpy==1.24.3 scikit-learn==1.3.0
pip install requests==2.31.0

# 4. Check if model exists
echo ""
echo "[4/5] Verifying model files..."
if [ ! -f "udare_model.tflite" ]; then
    echo "   [ERROR] udare_model.tflite DOES NOT EXIST!"
    echo "   [INFO] Train model on laptop with: python train.py"
    echo "   [INFO] Then transfer udare_model.tflite and scaler.pkl here"
    exit 1
fi

if [ ! -f "scaler.pkl" ]; then
    echo "   [ERROR] scaler.pkl DOES NOT EXIST!"
    echo "   [INFO] Train model on laptop with: python train.py"
    echo "   [INFO] Then transfer udare_model.tflite and scaler.pkl here"
    exit 1
fi

echo "   [OK] udare_model.tflite found"
echo "   [OK] scaler.pkl found"

# 5. Quick test
echo ""
echo "[5/5] Quick test..."
python app.py &
API_PID=$!
sleep 3

if curl -s http://localhost:5000/health > /dev/null; then
    echo "   [OK] API is working!"
else
    echo "   [ERROR] API not responding"
fi

kill $API_PID 2>/dev/null || true
wait $API_PID 2>/dev/null || true

# Summary
echo ""
echo "========================================================"
echo "           Installation completed successfully!         "
echo "========================================================"
echo ""
echo "To start the API:"
echo "   source venv/bin/activate"
echo "   python app.py"
echo ""
echo "To stop the API:"
echo "   Ctrl+C"
echo ""
echo "For testing:"
echo "   bash test_api.sh"
echo ""
