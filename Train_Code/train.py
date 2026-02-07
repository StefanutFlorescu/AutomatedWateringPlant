import numpy as np
import pandas as pd
import tensorflow as tf
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Dense
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import MinMaxScaler
import pickle

def train_model():
    # Load data
    data = pd.read_csv("water_data.csv")

    X = data[["temperature", "air_humidity", "luminosity"]]
    y = data["should_water"]

    # Normalization
    scaler = MinMaxScaler()
    X_scaled = scaler.fit_transform(X)

    # Save scaler for use in inference
    with open("scaler.pkl", "wb") as f:
        pickle.dump(scaler, f)

    # Train-test split
    X_train, X_test, y_train, y_test = train_test_split(X_scaled, y, test_size=0.2, random_state=42)

    # Create model
    model = Sequential([
        Dense(8, activation='relu', input_shape=(3,)),
        Dense(4, activation='relu'),
        Dense(1, activation='sigmoid')
    ])

    model.compile(optimizer='adam', loss='binary_crossentropy', metrics=['accuracy'])

    # Training
    history = model.fit(X_train, y_train, epochs=50, batch_size=8, validation_data=(X_test, y_test))

    # Testing
    loss, acc = model.evaluate(X_test, y_test)
    print(f"\n{'='*50}")
    print(f"Model Accuracy: {acc:.2%}")
    print(f"Model Loss: {loss:.4f}")
    print(f"{'='*50}\n")

    # Save model for Raspberry Pi
    model.save("udare_model.h5")
    print("Model saved as 'udare_model.h5'")
    print("Scaler saved as 'scaler.pkl'")
    
    # Convert to TensorFlow Lite for Raspberry Pi Zero
    print("\nConverting model to TensorFlow Lite...")
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    
    # Optimizations for Raspberry Pi Zero (ARMv6)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.target_spec.supported_types = [tf.float32]  # Use float32 for compatibility
    
    tflite_model = converter.convert()
    
    # Save TFLite model
    with open("udare_model.tflite", "wb") as f:
        f.write(tflite_model)
    
    print(f"[OK] TFLite model saved as 'udare_model.tflite' ({len(tflite_model)} bytes)")
    print(f"[OK] Model optimized for Raspberry Pi Zero (ARMv6/ARMv7)")

if __name__ == "__main__":
    train_model()
