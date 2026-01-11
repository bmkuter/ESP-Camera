#!/usr/bin/env python3
"""
WiFi Camera Client for ESP32-S3 Camera Server

This script connects to the ESP32 camera via WiFi HTTP API and captures images.

Usage:
    python capture_wifi.py <ESP32_IP_OR_HOSTNAME> [options]
    python capture_wifi.py 192.168.1.100
    python capture_wifi.py growpod-camera.local --auto-exposure --quality 10

Camera Settings:
    --auto-exposure          Enable auto exposure (default)
    --manual-exposure VALUE  Set manual exposure (0-1200)
    --exposure-comp VALUE    Exposure compensation (-2 to +2)
    --auto-gain              Enable auto gain (default)
    --manual-gain VALUE      Set manual gain (0-30)
    --quality VALUE          MJPEG stream quality (6-12, default 10)

API Endpoints:
    GET /          - Status page
    GET /capture   - Capture and download image
    GET /status    - Get camera status JSON
    GET /control   - Apply camera settings
"""

import sys
import requests
from datetime import datetime
import json
import time
import socket
import argparse

try:
    from zeroconf import Zeroconf, ServiceBrowser, ServiceListener
    ZEROCONF_AVAILABLE = True
except ImportError:
    ZEROCONF_AVAILABLE = False

# Store start time for relative timestamps
start_time = time.time()

# Resolution mapping (ESP32 framesize_t enum values from sensor.h)
# These match the actual enum order in espressif__esp32-camera/driver/include/sensor.h
RESOLUTIONS = {
    'qxga': 19,   # 2048x1536 - FRAMESIZE_QXGA
    'uxga': 15,   # 1600x1200 - FRAMESIZE_UXGA
    'sxga': 14,   # 1280x1024 - FRAMESIZE_SXGA
    'xga': 12,    # 1024x768  - FRAMESIZE_XGA
    'svga': 11,   # 800x600   - FRAMESIZE_SVGA
    'vga': 10,    # 640x480   - FRAMESIZE_VGA
    'hvga': 9,    # 480x320   - FRAMESIZE_HVGA
    'cif': 8,     # 400x296   - FRAMESIZE_CIF
    'qvga': 6,    # 320x240   - FRAMESIZE_QVGA
}

RESOLUTION_NAMES = {
    10: 'QXGA (2048x1536)',
    9: 'UXGA (1600x1200)',
    8: 'SXGA (1280x1024)',
    7: 'XGA (1024x768)',
    6: 'SVGA (800x600)',
    5: 'VGA (640x480)',
    4: 'HVGA (480x320)',
    3: 'CIF (400x296)',
    2: 'QVGA (320x240)',
}

def get_timestamp():
    """Get timestamp in milliseconds since script start (like ESP-IDF logging)"""
    elapsed_ms = int((time.time() - start_time) * 1000)
    return f"({elapsed_ms})"

def log(message, prefix="*"):
    """Print message with timestamp"""
    print(f"{get_timestamp()} [{prefix}] {message}")

class CameraServiceListener(ServiceListener):
    """Listener for mDNS camera service discovery"""
    def __init__(self, hostname):
        self.hostname = hostname
        self.ip_address = None
        self.found = False
    
    def add_service(self, zc, type_, name):
        info = zc.get_service_info(type_, name)
        if info:
            # Check if this is our camera
            server = info.server.rstrip('.')
            if server == self.hostname or server == f"{self.hostname}.local":
                if info.addresses:
                    self.ip_address = socket.inet_ntoa(info.addresses[0])
                    self.found = True
                    log(f"Found {self.hostname} at {self.ip_address}", "+")
    
    def remove_service(self, zc, type_, name):
        pass
    
    def update_service(self, zc, type_, name):
        pass

def resolve_mdns_fast(hostname, timeout=3.0):
    """
    Fast mDNS resolution using Zeroconf service discovery.
    Falls back to standard resolution if zeroconf is not available.
    
    Args:
        hostname: Hostname to resolve (e.g., "growpod-camera" or "growpod-camera.local")
        timeout: Maximum time to wait for resolution in seconds
    
    Returns:
        IP address string, or None if not resolved
    """
    if not ZEROCONF_AVAILABLE:
        return None
    
    # Strip .local suffix if present
    hostname_base = hostname.replace('.local', '')
    
    log(f"Resolving {hostname} via mDNS...")
    
    try:
        zeroconf = Zeroconf()
        listener = CameraServiceListener(hostname_base)
        browser = ServiceBrowser(zeroconf, "_http._tcp.local.", listener)
        
        # Wait for discovery with timeout
        start = time.time()
        while not listener.found and (time.time() - start) < timeout:
            time.sleep(0.1)
        
        browser.cancel()
        zeroconf.close()
        
        if listener.found:
            return listener.ip_address
        else:
            log(f"mDNS resolution timed out after {timeout}s", "!")
            return None
            
    except Exception as e:
        log(f"mDNS resolution error: {e}", "!")
        return None

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

def apply_camera_settings(esp32_host, aec=None, aec_value=None, ae_level=None, 
                          gain_ctrl=None, agc_gain=None, quality=None, framesize=None,
                          brightness=None, contrast=None, saturation=None, sharpness=None):
    """
    Apply camera settings to the ESP32.
    
    Args:
        esp32_host: IP address or hostname of the ESP32
        aec: Auto exposure control (0=manual, 1=auto)
        aec_value: Manual exposure value (0-1200)
        ae_level: Exposure compensation (-2 to +2)
        gain_ctrl: Auto gain control (0=manual, 1=auto)
        agc_gain: Manual gain value (0-30)
        quality: JPEG quality (0-63, lower is better quality)
        framesize: Frame size/resolution (0-10, see RESOLUTIONS dict)
        brightness: Brightness adjustment (-2 to +2)
        contrast: Contrast adjustment (-2 to +2)
        saturation: Saturation adjustment (-2 to +2)
        sharpness: Sharpness adjustment (-2 to +2)
    
    Returns:
        True if successful, False otherwise
    """
    settings = []
    
    if aec is not None:
        settings.append(('aec', aec))
    if aec_value is not None:
        settings.append(('aec_value', aec_value))
    if ae_level is not None:
        settings.append(('ae_level', ae_level))
    if gain_ctrl is not None:
        settings.append(('gain_ctrl', gain_ctrl))
    if agc_gain is not None:
        settings.append(('agc_gain', agc_gain))
    if quality is not None:
        settings.append(('quality', quality))
    if framesize is not None:
        settings.append(('framesize', framesize))
    if brightness is not None:
        settings.append(('brightness', brightness))
    if contrast is not None:
        settings.append(('contrast', contrast))
    if saturation is not None:
        settings.append(('saturation', saturation))
    if sharpness is not None:
        settings.append(('sharpness', sharpness))
    
    if not settings:
        log("No settings to apply", "!")
        return False
    
    log(f"Applying {len(settings)} camera setting(s)...")
    
    success = True
    for var, val in settings:
        url = f"http://{esp32_host}/control?var={var}&val={val}"
        
        try:
            response = requests.get(url, timeout=5)
            
            if response.status_code == 200:
                log(f"Set {var}={val} ✓", "+")
            else:
                log(f"Failed to set {var}={val} (status {response.status_code})", "!")
                success = False
                
        except Exception as e:
            log(f"Error setting {var}={val}: {e}", "!")
            success = False
    
    if success:
        log("All camera settings applied successfully", "+")
    
    return success

def print_help():
    """Print help information for interactive commands"""
    print("\n" + "=" * 60)
    print("CAMERA CONTROL COMMANDS")
    print("=" * 60)
    
    print("\nImage Capture:")
    print("  [ENTER]           - Capture and save image with current settings")
    print("  capture           - Same as pressing ENTER")
    
    print("\nCamera Information:")
    print("  status / s        - Show all camera settings and parameters")
    print("  resolutions       - List all available resolutions")
    
    print("\nExposure Control:")
    print("  auto              - Enable auto exposure and auto gain")
    print("  manual <VALUE>    - Set manual exposure (0-1200)")
    print("                      Examples: manual 300, manual 800")
    print("  comp <VALUE>      - Set exposure compensation (-2 to +2)")
    print("                      Examples: comp -1, comp 2")
    
    print("\nGain Control:")
    print("  gain <VALUE>      - Set manual gain (0-30)")
    print("                      Examples: gain 5, gain 15")
    
    print("\nImage Quality:")
    print("  quality <VALUE>   - Set JPEG quality (0-63, lower=better)")
    print("                      0-10  = Excellent quality, larger files")
    print("                      11-20 = Good quality")
    print("                      21+   = Lower quality, smaller files")
    print("                      Examples: quality 4, quality 12")
    
    print("\nImage Adjustments:")
    print("  brightness <VAL>  - Adjust brightness (-2 to +2)")
    print("                      Examples: brightness 1, brightness -1")
    print("  contrast <VALUE>  - Adjust contrast (-2 to +2)")
    print("                      Examples: contrast 1, contrast -2")
    print("  saturation <VAL>  - Adjust color saturation (-2 to +2)")
    print("                      Examples: saturation 1, saturation 0")
    print("  sharpness <VALUE> - Adjust sharpness (-2 to +2)")
    print("                      Examples: sharpness 1, sharpness -1")
    
    print("\nResolution:")
    print("  res <NAME>        - Set camera resolution")
    print("                      Available names: qxga, uxga, sxga, xga,")
    print("                      svga, vga, hvga, cif, qvga")
    print("                      Examples: res qxga, res vga")
    
    print("\nOther:")
    print("  help / h / ?      - Show this help message")
    print("  quit / q / exit   - Exit the program")
    
    print("\n" + "=" * 60)
    print("UNDERSTANDING EXPOSURE, GAIN, AND COMPENSATION")
    print("=" * 60)
    
    print("\n📷 EXPOSURE (Shutter Speed):")
    print("   - Controls how long the camera sensor collects light")
    print("   - Higher values = more light, slower capture, motion blur")
    print("   - Lower values = less light, faster capture, sharper motion")
    print("   - Range: 0-1200 (sensor-specific units)")
    print("   - Use case: Manual control for consistent lighting")
    
    print("\n🔆 GAIN (ISO/Amplification):")
    print("   - Amplifies the sensor signal electronically")
    print("   - Higher values = brighter image, MORE NOISE/GRAIN")
    print("   - Lower values = darker image, LESS NOISE/GRAIN")
    print("   - Range: 0-30")
    print("   - Use case: Brighten dark scenes without changing exposure")
    print("   - Warning: High gain reduces image quality!")
    
    print("\n⚖️  COMPENSATION (Exposure Bias):")
    print("   - Fine-tunes auto exposure decisions")
    print("   - Positive (+1, +2) = forces brighter images")
    print("   - Negative (-1, -2) = forces darker images")
    print("   - Range: -2 to +2")
    print("   - Use case: Override camera's auto exposure choice")
    print("   - Note: Only works when auto exposure is enabled!")
    
    print("\n💡 RECOMMENDED WORKFLOW:")
    print("   1. Start with 'auto' - let camera choose settings")
    print("   2. Use 'comp' to adjust brightness if auto is wrong")
    print("   3. For consistent results, use 'manual' for exposure")
    print("   4. Adjust 'gain' only if image is too dark")
    print("   5. Keep gain low (0-10) for best quality!")
    
    print("\n🎨 IMAGE ADJUSTMENTS (brightness, contrast, saturation, sharpness):")
    print("   - These are post-processing adjustments")
    print("   - Range: -2 to +2 for all adjustments")
    print("   - Default: 0 (no adjustment)")
    print("   - Brightness: Makes image lighter/darker overall")
    print("   - Contrast: Difference between light and dark areas")
    print("   - Saturation: Color intensity (0=grayscale, +2=vivid)")
    print("   - Sharpness: Edge definition (-2=soft, +2=sharp)")
    print("   - Tip: Start at 0 and adjust in small increments!")
    
    print("\n" + "=" * 60)
    print("TIP: Type 'status' to see all current settings.")
    print("=" * 60)

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
        print(f"  Quality: {status.get('quality', 0)} (0=best, 63=worst)")
        print(f"  PSRAM: {status.get('psram', False)}")
        
        # Exposure settings
        print(f"\nExposure:")
        aec = status.get('aec', 0)
        print(f"  Auto Exposure: {'Enabled' if aec else 'Disabled'}")
        if not aec:
            print(f"  Manual Exposure: {status.get('aec_value', 0)}")
        print(f"  Exposure Compensation: {status.get('ae_level', 0)}")
        
        # Gain settings
        print(f"\nGain:")
        agc = status.get('gain_ctrl', 0)
        print(f"  Auto Gain: {'Enabled' if agc else 'Disabled'}")
        if not agc:
            print(f"  Manual Gain: {status.get('agc_gain', 0)}")
        
        # Image adjustments
        print(f"\nImage Adjustments:")
        print(f"  Brightness: {status.get('brightness', 0)}")
        print(f"  Contrast: {status.get('contrast', 0)}")
        print(f"  Saturation: {status.get('saturation', 0)}")
        print(f"  Sharpness: {status.get('sharpness', 0)}")
        
        # Other settings
        print(f"\nOther Settings:")
        print(f"  Auto White Balance: {'Enabled' if status.get('awb', 0) else 'Disabled'}")
        print(f"  Horizontal Mirror: {'Yes' if status.get('hmirror', 0) else 'No'}")
        print(f"  Vertical Flip: {'Yes' if status.get('vflip', 0) else 'No'}")
        
        print("=" * 30)
    else:
        log("Failed to get status", "!")

def interactive_mode(esp32_host):
    """Interactive mode for capturing multiple images"""
    print("=" * 50)
    print(f"{get_timestamp()} ESP32 Camera Client - Connected to {esp32_host}")
    print("=" * 50)
    
    # Warn about mDNS delays on Windows
    if esp32_host.endswith('.local'):
        print("\nNote: Using mDNS hostname (.local) on Windows can be slow.")
        print("      For faster response, use the IP address directly.")
        print("      Check serial monitor for the IP address.\n")
    
    # Get initial status
    print_status(esp32_host)
    
    print("\nCommands: [ENTER]=capture, status, auto, manual, gain, comp, quality,")
    print("          brightness, contrast, saturation, sharpness, res, help, quit")
    print("Type 'help' or '?' for detailed command information")
    print("-" * 50)
    
    while True:
        try:
            command = input("\n> ").strip()
            
            # Handle empty command (capture)
            if command == '':
                capture_image(esp32_host)
                continue
            
            command_lower = command.lower()
            parts = command.split()
            
            if command_lower in ['q', 'quit', 'exit']:
                print("Goodbye!")
                break
            elif command_lower in ['h', 'help', '?']:
                print_help()
            elif command_lower in ['s', 'status']:
                print_status(esp32_host)
            elif command_lower == 'auto':
                # Enable auto exposure and auto gain
                apply_camera_settings(esp32_host, aec=1, gain_ctrl=1)
            elif command_lower == 'capture':
                capture_image(esp32_host)
            elif len(parts) >= 2 and parts[0] == 'manual':
                # Set manual exposure
                try:
                    exp_value = int(parts[1])
                    if 0 <= exp_value <= 1200:
                        apply_camera_settings(esp32_host, aec=0, aec_value=exp_value)
                    else:
                        print("Error: Exposure value must be between 0 and 1200")
                except ValueError:
                    print("Error: Invalid exposure value")
            elif len(parts) >= 2 and parts[0] == 'gain':
                # Set manual gain
                try:
                    gain_value = int(parts[1])
                    if 0 <= gain_value <= 30:
                        apply_camera_settings(esp32_host, gain_ctrl=0, agc_gain=gain_value)
                    else:
                        print("Error: Gain value must be between 0 and 30")
                except ValueError:
                    print("Error: Invalid gain value")
            elif len(parts) >= 2 and parts[0] == 'comp':
                # Set exposure compensation
                try:
                    comp_value = int(parts[1])
                    if -2 <= comp_value <= 2:
                        apply_camera_settings(esp32_host, ae_level=comp_value)
                    else:
                        print("Error: Compensation value must be between -2 and 2")
                except ValueError:
                    print("Error: Invalid compensation value")
            elif len(parts) >= 2 and parts[0] == 'quality':
                # Set JPEG quality
                try:
                    quality_value = int(parts[1])
                    if 0 <= quality_value <= 63:
                        apply_camera_settings(esp32_host, quality=quality_value)
                        print(f"Note: Lower quality numbers = better image quality (0=best, 63=worst)")
                    else:
                        print("Error: Quality value must be between 0 and 63")
                except ValueError:
                    print("Error: Invalid quality value")
            elif len(parts) >= 2 and parts[0] == 'brightness':
                # Set brightness
                try:
                    brightness_value = int(parts[1])
                    if -2 <= brightness_value <= 2:
                        apply_camera_settings(esp32_host, brightness=brightness_value)
                        print(f"Brightness set to {brightness_value}")
                    else:
                        print("Error: Brightness value must be between -2 and 2")
                except ValueError:
                    print("Error: Invalid brightness value")
            elif len(parts) >= 2 and parts[0] == 'contrast':
                # Set contrast
                try:
                    contrast_value = int(parts[1])
                    if -2 <= contrast_value <= 2:
                        apply_camera_settings(esp32_host, contrast=contrast_value)
                        print(f"Contrast set to {contrast_value}")
                    else:
                        print("Error: Contrast value must be between -2 and 2")
                except ValueError:
                    print("Error: Invalid contrast value")
            elif len(parts) >= 2 and parts[0] == 'saturation':
                # Set saturation
                try:
                    saturation_value = int(parts[1])
                    if -2 <= saturation_value <= 2:
                        apply_camera_settings(esp32_host, saturation=saturation_value)
                        print(f"Saturation set to {saturation_value}")
                    else:
                        print("Error: Saturation value must be between -2 and 2")
                except ValueError:
                    print("Error: Invalid saturation value")
            elif len(parts) >= 2 and parts[0] == 'sharpness':
                # Set sharpness
                try:
                    sharpness_value = int(parts[1])
                    if -2 <= sharpness_value <= 2:
                        apply_camera_settings(esp32_host, sharpness=sharpness_value)
                        print(f"Sharpness set to {sharpness_value}")
                    else:
                        print("Error: Sharpness value must be between -2 and 2")
                except ValueError:
                    print("Error: Invalid sharpness value")
            elif len(parts) >= 2 and parts[0] == 'res':
                # Set resolution
                res_name = parts[1].lower()
                if res_name in RESOLUTIONS:
                    framesize = RESOLUTIONS[res_name]
                    apply_camera_settings(esp32_host, framesize=framesize)
                    print(f"Resolution set to {RESOLUTION_NAMES[framesize]}")
                else:
                    print(f"Error: Unknown resolution '{parts[1]}'")
                    print(f"Available: {', '.join(RESOLUTIONS.keys())}")
            elif command_lower == 'resolutions':
                # List available resolutions
                print("\nAvailable Resolutions:")
                for name, value in sorted(RESOLUTIONS.items(), key=lambda x: x[1], reverse=True):
                    print(f"  {name:8s} - {RESOLUTION_NAMES[value]}")
            else:
                print(f"Unknown command: '{command}'")
                print("Type 'help' or '?' for available commands, or press ENTER to capture")
                
        except KeyboardInterrupt:
            print("\n\nInterrupted by user. Goodbye!")
            break
        except EOFError:
            break

def main():
    parser = argparse.ArgumentParser(
        description='ESP32 Camera WiFi Client',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Interactive mode
  python capture_wifi.py 192.168.1.100
  python capture_wifi.py growpod-camera.local
  
  # Single capture with settings
  python capture_wifi.py 192.168.1.100 --auto-exposure --auto-gain
  python capture_wifi.py 192.168.1.100 --manual-exposure 300 --manual-gain 10
  
  # Single capture with filename
  python capture_wifi.py 192.168.1.100 my_photo.jpg --exposure-comp -1
        """)
    
    parser.add_argument('host', help='ESP32 IP address or hostname')
    parser.add_argument('output', nargs='?', help='Output filename (optional, for single capture)')
    
    # Camera settings
    parser.add_argument('--auto-exposure', action='store_true', help='Enable auto exposure')
    parser.add_argument('--manual-exposure', type=int, metavar='VALUE', help='Manual exposure value (0-1200)')
    parser.add_argument('--exposure-comp', type=int, metavar='VALUE', help='Exposure compensation (-2 to +2)')
    parser.add_argument('--auto-gain', action='store_true', help='Enable auto gain')
    parser.add_argument('--manual-gain', type=int, metavar='VALUE', help='Manual gain value (0-30)')
    parser.add_argument('--quality', type=int, metavar='VALUE', help='JPEG quality (0-63, lower is better, default 4)')
    parser.add_argument('--resolution', type=str, metavar='NAME', 
                        choices=list(RESOLUTIONS.keys()),
                        help='Resolution (qxga, uxga, sxga, xga, svga, vga, hvga, cif, qvga)')
    
    args = parser.parse_args()
    
    esp32_host = args.host
    
    # Strip http:// or https:// prefix if provided
    esp32_host = esp32_host.replace('http://', '').replace('https://', '')
    # Strip trailing slash
    esp32_host = esp32_host.rstrip('/')
    
    # Try fast mDNS resolution if hostname ends with .local
    if esp32_host.endswith('.local') or ('.' not in esp32_host):
        if not ZEROCONF_AVAILABLE:
            print("\n" + "=" * 60)
            print("ERROR: 'zeroconf' library is required for mDNS hostnames")
            print("=" * 60)
            print("\nTo fix this issue:")
            print("  pip install zeroconf")
            print("\nAlternatively, use the IP address directly:")
            print("  python capture_wifi.py 192.168.x.x")
            print("\nCheck the serial monitor for the camera's IP address.")
            print("=" * 60)
            return 1
        
        resolved_ip = resolve_mdns_fast(esp32_host, timeout=3.0)
        if resolved_ip:
            log(f"Using resolved IP: {resolved_ip}", "+")
            esp32_host = resolved_ip
        else:
            log(f"mDNS resolution failed - camera not found", "!")
            log(f"Make sure the camera is powered on and connected to WiFi", "!")
            return 1
    
    # Apply camera settings if specified
    settings_changed = False
    if args.auto_exposure or args.manual_exposure is not None:
        aec = 1 if args.auto_exposure else 0
        aec_value = args.manual_exposure if args.manual_exposure is not None else None
        settings_changed = apply_camera_settings(esp32_host, aec=aec, aec_value=aec_value) or settings_changed
    
    if args.exposure_comp is not None:
        if -2 <= args.exposure_comp <= 2:
            settings_changed = apply_camera_settings(esp32_host, ae_level=args.exposure_comp) or settings_changed
        else:
            print("Error: Exposure compensation must be between -2 and 2")
            return 1
    
    if args.auto_gain or args.manual_gain is not None:
        gain_ctrl = 1 if args.auto_gain else 0
        agc_gain = args.manual_gain if args.manual_gain is not None else None
        settings_changed = apply_camera_settings(esp32_host, gain_ctrl=gain_ctrl, agc_gain=agc_gain) or settings_changed
    
    if args.quality is not None:
        if 0 <= args.quality <= 63:
            settings_changed = apply_camera_settings(esp32_host, quality=args.quality) or settings_changed
        else:
            print("Error: Quality must be between 0 and 63")
            return 1
    
    if args.resolution is not None:
        framesize = RESOLUTIONS[args.resolution.lower()]
        settings_changed = apply_camera_settings(esp32_host, framesize=framesize) or settings_changed
        print(f"Resolution set to {RESOLUTION_NAMES[framesize]}")
    
    # Wait a moment for settings to take effect
    if settings_changed:
        time.sleep(0.5)
    
    # Check if output file is specified (single capture mode)
    if args.output:
        success = capture_image(esp32_host, args.output)
        return 0 if success else 1
    else:
        # Interactive mode
        interactive_mode(esp32_host)
        return 0

if __name__ == "__main__":
    sys.exit(main())
