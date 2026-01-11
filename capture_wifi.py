#!/usr/bin/env python3
"""
WiFi Camera Client for ESP32-S3 Camera Server

This script connects to the ESP32 camera via WiFi HTTP API and captures images.

Usage:
    python capture_wifi.py <ESP32_IP_OR_HOSTNAME>
    python capture_wifi.py 192.168.1.100
    python capture_wifi.py esp-planter-camera.local

API Endpoints:
    GET /          - Status page
    GET /capture   - Capture and download image
    GET /status    - Get camera status JSON
"""

import sys
import requests
from datetime import datetime
import json
import time

# Store start time for relative timestamps
start_time = time.time()

def get_timestamp():
    """Get timestamp in milliseconds since script start (like ESP-IDF logging)"""
    elapsed_ms = int((time.time() - start_time) * 1000)
    return f"({elapsed_ms})"

def log(message, prefix="*"):
    """Print message with timestamp"""
    print(f"{get_timestamp()} [{prefix}] {message}")

def capture_image(esp32_host, output_file=None):
    """
    Capture an image from the ESP32 camera via HTTP.
    
    Args:
        esp32_host: IP address or hostname (e.g., "192.168.1.100" or "esp-planter-camera.local")
        output_file: Optional filename for saved image (auto-generated if None)
    
    Returns:
        True if successful, False otherwise
    """
    url = f"http://{esp32_host}/capture"
    
    log(f"Requesting image from {url}...")
    
    try:
        request_start = time.time()
        response = requests.get(url, timeout=30)
        request_time = (time.time() - request_start) * 1000  # Convert to ms
        
        if response.status_code == 200:
            # Generate filename if not provided
            if output_file is None:
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                output_file = f"capture_{timestamp}.jpg"
            
            # Save image
            with open(output_file, 'wb') as f:
                f.write(response.content)
            
            log(f"Image saved: {output_file}", "+")
            log(f"Size: {len(response.content):,} bytes, Total time: {request_time:.0f} ms", "+")
            return True
        else:
            log(f"Error: Server returned status code {response.status_code}", "!")
            log(f"{response.text}", "!")
            return False
            
    except requests.exceptions.Timeout:
        log("Error: Request timed out (camera may be processing)", "!")
        return False
    except requests.exceptions.ConnectionError:
        log(f"Error: Could not connect to {esp32_host}", "!")
        log("Make sure the ESP32 is powered on and connected to WiFi", "!")
        return False
    except Exception as e:
        log(f"Error: {e}", "!")
        return False

def get_status(esp32_host):
    """
    Get camera status from ESP32.
    
    Args:
        esp32_host: IP address or hostname of the ESP32
    
    Returns:
        Dictionary with status info, or None if failed
    """
    url = f"http://{esp32_host}/status"
    
    try:
        response = requests.get(url, timeout=5)
        
        if response.status_code == 200:
            return response.json()
        else:
            log(f"Error: Server returned status code {response.status_code}", "!")
            return None
            
    except Exception as e:
        log(f"Error getting status: {e}", "!")
        return None

def print_status(esp32_host):
    """Print camera status in a formatted way"""
    log(f"Getting status from {esp32_host}...")
    status = get_status(esp32_host)
    
    if status:
        print("\n=== Camera Status ===")
        print(f"Status: {status.get('status', 'unknown')}")
        print(f"\nCamera:")
        print(f"  Model: {status.get('camera', 'unknown')}")
        print(f"  Resolution: {status.get('resolution', 'unknown')} ({status.get('width', 0)}x{status.get('height', 0)})")
        print(f"  Format: {status.get('format', 'unknown')}")
        print(f"  PSRAM: {status.get('psram', False)}")
        print("=" * 30)
    else:
        log("Failed to get status", "!")

def interactive_mode(esp32_host):
    """Interactive mode for capturing multiple images"""
    print("=" * 50)
    print(f"{get_timestamp()} ESP32 Camera Client - Connected to {esp32_host}")
    print("=" * 50)
    
    # Get initial status
    print_status(esp32_host)
    
    print("\nCommands:")
    print("  [ENTER]     - Capture image")
    print("  status / s  - Get camera status")
    print("  quit / q    - Exit")
    print("-" * 50)
    
    while True:
        try:
            command = input("\n> ").strip().lower()
            
            if command in ['q', 'quit', 'exit']:
                print("Goodbye!")
                break
            elif command in ['s', 'status']:
                print_status(esp32_host)
            elif command == '' or command == 'capture':
                capture_image(esp32_host)
            else:
                print(f"Unknown command: {command}")
                print("Press ENTER to capture, 's' for status, or 'q' to quit")
                
        except KeyboardInterrupt:
            print("\n\nInterrupted by user. Goodbye!")
            break
        except EOFError:
            break

def main():
    if len(sys.argv) < 2:
        print("Usage: python capture_wifi.py <ESP32_HOST> [output_file]")
        print("\nExamples:")
        print("  python capture_wifi.py 192.168.1.100")
        print("  python capture_wifi.py growpod-camera.local")
        print("  python capture_wifi.py 192.168.1.100 my_photo.jpg")
        print("\nInteractive mode:")
        print("  python capture_wifi.py growpod-camera.local")
        print("  (then press ENTER repeatedly to capture images)")
        return 1
    
    esp32_host = sys.argv[1]
    
    # Strip http:// or https:// prefix if provided
    esp32_host = esp32_host.replace('http://', '').replace('https://', '')
    # Strip trailing slash
    esp32_host = esp32_host.rstrip('/')
    
    # Check if output file is specified (single capture mode)
    if len(sys.argv) >= 3:
        output_file = sys.argv[2]
        success = capture_image(esp32_host, output_file)
        return 0 if success else 1
    else:
        # Interactive mode
        interactive_mode(esp32_host)
        return 0

if __name__ == "__main__":
    sys.exit(main())
