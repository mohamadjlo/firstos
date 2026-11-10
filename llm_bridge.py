import subprocess
import threading
import sys
import os
import urllib.request
import json

SYSTEM_PROMPT = """You are an AI assistant built into FirstOS, a lightweight custom operating system. 
You can respond to the user and answer their questions.
If the user asks you to perform an action on the OS (like creating a file, printing text, math, etc.), you can execute terminal commands!
To execute a command, simply put the command on a new line prefixed with "LLM_CMD:". 
For example, if the user asks you to create a file named test.txt:
LLM_CMD:touch test.txt
If they ask you to write "hello" to it:
LLM_CMD:write test.txt hello
Other commands you might use:
cat <file>
echo <text>
calc 1 + 1
clear
fl run <script.fl>
Do NOT use markdown code blocks for the commands, just the raw text prefixed with LLM_CMD:.
"""

def query_ollama(prompt):
    print(f"[LLM Bridge] Querying Ollama REST API...")
    full_prompt = f"{SYSTEM_PROMPT}\nUser Request: {prompt}"
    
    url = "http://localhost:11434/api/generate"
    data = json.dumps({
        "model": "qwen2.5:1.5b",
        "prompt": full_prompt,
        "stream": False
    }).encode('utf-8')
    
    req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req) as response:
            result = json.loads(response.read().decode('utf-8'))
            return result.get("response", "")
    except Exception as e:
        return f"Ollama Error: {str(e)}"

def main():
    print("[LLM Bridge] Starting QEMU...")
    
    cmd = ["qemu-system-i386", "-cdrom", "firstos.iso", "-m", "512M", 
           "-serial", "stdio", "-vga", "none", "-device", "virtio-gpu-pci,xres=1024,yres=768", 
           "-usb", "-device", "usb-tablet", 
           "-drive", "file=fs.img,format=raw,if=none,id=fsdisk", 
           "-device", "virtio-blk-pci,drive=fsdisk", 
           "-display", "cocoa,zoom-to-fit=on"]
           
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=1, universal_newlines=True)

    def read_output():
        while True:
            line = process.stdout.readline()
            if not line:
                break
            line = line.strip()
            print(f"[QEMU] {line}")
            
            if line.startswith("LLM_REQ:"):
                prompt = line[8:].strip()
                print(f"[LLM] Received Prompt: {prompt}")
                
                # Real LLM Response via Ollama API
                response = query_ollama(prompt)
                
                print("[LLM] Sending response back to FirstOS...")
                for r_line in response.split('\n'):
                    process.stdin.write(r_line + '\n')
                
                process.stdin.write("LLM_EOF\n")
                process.stdin.flush()
                print("[LLM] Done sending.")
                
    t = threading.Thread(target=read_output, daemon=True)
    t.start()
    
    try:
        process.wait()
    except KeyboardInterrupt:
        process.terminate()
        
    print("[LLM Bridge] QEMU closed.")

if __name__ == "__main__":
    main()
