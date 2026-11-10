import json

with open('/Users/mohamadjlo/.gemini/antigravity-cli/brain/4226e84c-8663-470b-9ef7-409b6883be31/.system_generated/logs/transcript_full.jsonl', 'r') as f:
    for line in f:
        try:
            d = json.loads(line)
            # Check for view_file gui.c
            if d.get('type') == 'PLANNER_RESPONSE':
                for tc in d.get('tool_calls', []):
                    if tc.get('name') == 'default_api:view_file' and 'gui.c' in str(tc):
                        print('Found view_file gui.c in step', d.get('step_index'))
            
            # Check for tool_response of view_file
            if d.get('type') == 'TOOL_RESPONSE' and 'gui.c' in d.get('content', '') and 'int ide_saving_ui' in d.get('content', ''):
                pass # print('Found gui.c content in step', d.get('step_index'))
                
        except Exception as e:
            pass
