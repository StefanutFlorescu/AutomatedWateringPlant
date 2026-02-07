from flask import Flask, request, jsonify
import numpy as np
import pickle
import os

# Import TFLite runtime for Raspberry Pi Zero
try:
    import tflite_runtime.interpreter as tflite
    TFLITE_AVAILABLE = True
except ImportError:
    print("tflite_runtime is not available, trying tensorflow.lite...")
    try:
        import tensorflow.lite as tflite
        TFLITE_AVAILABLE = True
    except ImportError:
        print("No TFLite runtime available!")
        TFLITE_AVAILABLE = False
        tflite = None

app = Flask(__name__)

# Load TFLite model and scaler at startup
MODEL_PATH = "udare_model.tflite"
SCALER_PATH = "scaler.pkl"

interpreter = None
input_details = None
output_details = None

if TFLITE_AVAILABLE:
    try:
        # Load TFLite model
        interpreter = tflite.Interpreter(model_path=MODEL_PATH)
        interpreter.allocate_tensors()
        
        # Get input/output details
        input_details = interpreter.get_input_details()
        output_details = interpreter.get_output_details()
        
        print(f" TFLite model loaded from {MODEL_PATH}")
        print(f"  Input shape: {input_details[0]['shape']}")
        print(f"  Output shape: {output_details[0]['shape']}")
    except Exception as e:
        print(f" Error loading TFLite model: {e}")
        interpreter = None
else:
    print(" TFLite runtime is not available")

try:
    with open(SCALER_PATH, "rb") as f:
        scaler = pickle.load(f)
    print(f" Scaler loaded from {SCALER_PATH}")
except Exception as e:
    print(f" Error loading scaler: {e}")
    scaler = None


@app.route("/")
def home():
    """Endpoint for status check"""
    return jsonify({
        "status": "running",
        "service": "Automated Watering System API (TFLite)",
        "model_loaded": interpreter is not None,
        "scaler_loaded": scaler is not None,
        "optimized_for": "Raspberry Pi Zero (ARMv6/ARMv7)"
    })


@app.route("/health")
def health():
    """Health check endpoint"""
    if interpreter is None or scaler is None:
        return jsonify({"status": "unhealthy", "reason": "Model or scaler not loaded"}), 500
    return jsonify({"status": "healthy"}), 200


@app.route("/predict", methods=["POST"])
def predict():
    """
    Endpoint for prediction
    
    Expected JSON body:
    {
        "temperature": 25.5,
        "air_humidity": 45.0,
        "luminosity": 800.0
    }
    """
    if interpreter is None or scaler is None:
        return jsonify({
            "error": "Model or scaler not available"
        }), 500
    
    try:
        # Get data from request
        data = request.get_json()
        
        # Validate data
        required_fields = ["temperature", "air_humidity", "luminosity"]
        if not all(field in data for field in required_fields):
            return jsonify({
                "error": f"Missing required fields. Required: {required_fields}"
            }), 400
        
        # Prepare data for prediction
        input_data = np.array([[
            float(data["temperature"]),
            float(data["air_humidity"]),
            float(data["luminosity"])
        ]], dtype=np.float32)
        
        # Normalize data
        input_scaled = scaler.transform(input_data).astype(np.float32)
        
        # Make prediction with TFLite
        interpreter.set_tensor(input_details[0]['index'], input_scaled)
        interpreter.invoke()
        prediction = interpreter.get_tensor(output_details[0]['index'])[0][0]
        
        # Convert to boolean (threshold 0.5)
        should_water = bool(prediction >= 0.5)
        
        return jsonify({
            "should_water": should_water,
            "probability": float(prediction),
            "input": {
                "temperature": float(data["temperature"]),
                "air_humidity": float(data["air_humidity"]),
                "luminosity": float(data["luminosity"])
            }
        })
    
    except ValueError as e:
        return jsonify({
            "error": f"Invalid data: {str(e)}"
        }), 400
    except Exception as e:
        return jsonify({
            "error": f"Prediction error: {str(e)}"
        }), 500


@app.route("/batch_predict", methods=["POST"])
def batch_predict():
    """
    Endpoint for batch predictions
    
    Expected JSON body:
    {
        "data": [
            {"temperature": 25.5, "air_humidity": 45.0, "luminosity": 800.0},
            {"temperature": 30.0, "air_humidity": 30.0, "luminosity": 1000.0}
        ]
    }
    """
    if interpreter is None or scaler is None:
        return jsonify({
            "error": "Model or scaler not available"
        }), 500
    
    try:
        # Get data from request
        request_data = request.get_json()
        
        if "data" not in request_data or not isinstance(request_data["data"], list):
            return jsonify({
                "error": "Expecting a 'data' field with a list of measurements"
            }), 400
        
        results = []
        required_fields = ["temperature", "air_humidity", "luminosity"]
        
        for i, data in enumerate(request_data["data"]):
            # Validate data
            if not all(field in data for field in required_fields):
                return jsonify({
                    "error": f"Missing fields at input {i}. Required: {required_fields}"
                }), 400
            
            # Prepare data
            input_data = np.array([[
                float(data["temperature"]),
                float(data["air_humidity"]),
                float(data["luminosity"])
            ]], dtype=np.float32)
            
            # Normalize and predict with TFLite
            input_scaled = scaler.transform(input_data).astype(np.float32)
            interpreter.set_tensor(input_details[0]['index'], input_scaled)
            interpreter.invoke()
            prediction = interpreter.get_tensor(output_details[0]['index'])[0][0]
            
            results.append({
                "should_water": bool(prediction >= 0.5),
                "probability": float(prediction),
                "input": {
                    "temperature": float(data["temperature"]),
                    "air_humidity": float(data["air_humidity"]),
                    "luminosity": float(data["luminosity"])
                }
            })
        
        return jsonify({
            "predictions": results,
            "total": len(results)
        })
    
    except ValueError as e:
        return jsonify({
            "error": f"Invalid data: {str(e)}"
        }), 400
    except Exception as e:
        return jsonify({
            "error": f"Prediction error: {str(e)}"
        }), 500


if __name__ == "__main__":
    port = int(os.environ.get("PORT", 5000))
    app.run(host="0.0.0.0", port=port, debug=False)