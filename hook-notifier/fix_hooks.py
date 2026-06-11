import json

with open(r'C:\Users\joe06\.claude\settings.json', 'r') as f:
    data = json.load(f)

WORKING_CMD = (
    'powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden '
    '-Command "$c=New-Object Net.Sockets.TcpClient(\'127.0.0.1\',54321);'
    '$s=$c.GetStream();$b=[Text.Encoding]::ASCII.GetBytes(\'WORKING\n\');'
    '$s.Write($b,0,$b.Length);$c.Close()"'
)

WAITING_CMD = WORKING_CMD.replace('WORKING', 'WAITING_USER')

# PostToolUse - replace python with PowerShell
for entry in data['hooks'].get('PostToolUse', []):
    for h in entry.get('hooks', []):
        if h['type'] == 'command' and 'notify.py' in h['command']:
            h['command'] = WORKING_CMD
            print('PostToolUse: replaced')

# Notification - replace python with PowerShell
for entry in data['hooks'].get('Notification', []):
    for h in entry.get('hooks', []):
        if h['type'] == 'command' and 'notify.py' in h['command']:
            h['command'] = WAITING_CMD
            print('Notification: replaced')

# Stop - keep Python
print('Stop: kept as-is')

with open(r'C:\Users\joe06\.claude\settings.json', 'w') as f:
    json.dump(data, f, indent=2, ensure_ascii=False)

print('Done')
# Verify
print('\n--- PostToolUse command ---')
for entry in data['hooks']['PostToolUse']:
    for h in entry['hooks']:
        print(h['command'][:120])
print('\n--- Notification command ---')
for entry in data['hooks']['Notification']:
    for h in entry['hooks']:
        print(h['command'][:120])
