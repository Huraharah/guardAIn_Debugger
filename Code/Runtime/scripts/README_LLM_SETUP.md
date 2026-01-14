# LLM Integration Setup Guide

This guide explains how to set up the LLM integration for automatic GDB debug plan generation.

## Overview

The LLM integration automatically analyzes static analysis artifacts (objdump, strings, readelf, etc.) and generates a GDB debug plan JSON that identifies:
- Critical breakpoints (syscalls, anti-debugging, encryption, etc.)
- Appropriate actions (register manipulation, snapshots, file captures)
- Key decision points in the code

## Setup Options

### Option 1: OpenAI API (Default)

The Python script uses OpenAI API by default. **For large prompts (>30k tokens), use `gpt-4o-mini` which has a 128k context window.**

1. **Install Python dependencies:**
   ```bash
   pip install openai
   ```

2. **Set your OpenAI API key:**
   ```bash
   # Windows (PowerShell)
   $env:OPENAI_API_KEY = "your-api-key-here"
   
   # Windows (CMD)
   set OPENAI_API_KEY=your-api-key-here
   
   # Linux/Mac
   export OPENAI_API_KEY=your-api-key-here
   ```

3. **Optionally set the model** (defaults to `gpt-4o-mini` for large contexts):
   ```bash
   # Recommended for large prompts (128k context, very cheap)
   $env:LLM_MODEL = "gpt-4o-mini"
   
   # Other options:
   # $env:LLM_MODEL = "gpt-4o"          # 30k context limit
   # $env:LLM_MODEL = "gpt-4-turbo"     # 128k context
   ```

4. **The system will automatically use the API key and model** when generating plans.

### Option 1a: Claude API (Recommended for Very Large Contexts)

Claude 3.5 Sonnet has a 200k token context window and excellent code generation capabilities. **Best for large prompts.**

1. **Install Python dependencies:**
   ```bash
   pip install anthropic
   ```

2. **Set your Anthropic API key:**
   ```bash
   # Windows (PowerShell)
   $env:ANTHROPIC_API_KEY = "your-api-key-here"
   
   # Windows (CMD)
   set ANTHROPIC_API_KEY=your-api-key-here
   
   # Linux/Mac
   export ANTHROPIC_API_KEY=your-api-key-here
   ```

3. **Set the provider to Claude:**
   ```bash
   $env:LLM_PROVIDER = "claude"
   ```

4. **Optionally set the model** (defaults to `claude-3-5-sonnet-20241022`):
   ```bash
   # Best model for code (200k context, ~$3/$15 per 1M tokens)
   $env:LLM_MODEL = "claude-3-5-sonnet-20241022"
   
   # Cheaper alternative (200k context, ~$0.75/$3 per 1M tokens)
   # $env:LLM_MODEL = "claude-3-haiku-20240307"
   ```

### Option 2: Manual Cursor Integration (Alternative)

If you prefer to use Cursor directly:

1. The system will generate a prompt file at: `artifacts/<sample_name>/LLM/prompt.txt`

2. Open the prompt file in Cursor and ask:
   ```
   Analyze this prompt and generate a JSON debug plan according to the format specified in the prompt.
   Only output the JSON, no markdown, no explanations.
   ```

3. Copy the JSON output and save it to: `artifacts/<sample_name>/LLM/plan.json`

4. The system will then use this plan.json to generate the GDB script.

### Option 3: Custom LLM Provider

To use a different LLM provider, modify `generate_plan.py`:

1. Edit the `generate_plan_with_openai()` function or add a new function
2. Implement the API call for your provider
3. Update the `main()` function to use your implementation

## How It Works

1. **Static Analysis Phase**: The system runs objdump, strings, readelf, etc. on the sample
2. **Prompt Generation**: Static artifacts are collected and formatted into a comprehensive prompt
3. **LLM Processing**: The prompt is sent to the LLM (via Python script) to generate plan.json
4. **GDB Script Generation**: The plan.json is converted into a GDB script by `GdbScriptBuilder`
5. **Debug Execution**: The GDB script is executed in the QEMU VM

## File Locations

- **Prompt file**: `artifacts/<sample_name>/LLM/prompt.txt`
- **Generated plan**: `artifacts/<sample_name>/LLM/plan.json`
- **GDB script**: `artifacts/<sample_name>/debug/plan.gdb`
- **Python script**: `guardAInDBG/Code/Runtime/scripts/generate_plan.py`

## Model Selection Guide

### For Large Prompts (70k+ tokens)

**Recommended: Claude 3.5 Sonnet**
- 200k token context window
- Excellent code generation
- ~$3 per 1M input tokens, ~$15 per 1M output tokens
- Set: `$env:LLM_PROVIDER = "claude"`

**Alternative: GPT-4o-mini**
- 128k token context window
- Very cheap (~$0.15/$0.60 per 1M tokens)
- Good quality, lower than Claude
- Set: `$env:LLM_MODEL = "gpt-4o-mini"`

### For Smaller Prompts (<30k tokens)

**GPT-4o** (default for small prompts)
- 30k token context limit
- Best quality
- More expensive

## Troubleshooting

### Python script not found
- Ensure the script is at: `guardAInDBG/Code/Runtime/scripts/generate_plan.py`
- Or adjust the search paths in `LlmInterface.cpp`

### Token limit errors
- Your prompt is too large for the selected model
- Switch to a model with larger context:
  - Claude: Set `$env:LLM_PROVIDER = "claude"` (200k context)
  - GPT-4o-mini: Set `$env:LLM_MODEL = "gpt-4o-mini"` (128k context)

### API errors
- **OpenAI**: Verify your API key: `echo %OPENAI_API_KEY%` (Windows)
- **Claude**: Verify your API key: `echo %ANTHROPIC_API_KEY%` (Windows)
- Check your API key has sufficient credits
- Ensure you have internet connectivity

### Invalid JSON generated
- The LLM may sometimes add markdown formatting
- The script attempts to extract JSON from markdown, but manual correction may be needed
- Check the prompt.txt file - it should be well-formatted

### Plan generation fails silently
- Check the logs for error messages
- Verify static analysis artifacts exist in `artifacts/<sample_name>/static/`
- Ensure objdump, strings, and readelf outputs were generated successfully

## Example Workflow

```
1. Run analysis: guardAInDBG.exe
2. System runs static analysis → generates artifacts
3. System generates prompt.txt from artifacts
4. System calls Python script → generates plan.json
5. System converts plan.json → plan.gdb
6. System runs GDB with plan.gdb in QEMU VM
```

## Advanced Configuration

### Customizing the Prompt

Edit `LlmInterface::buildPrompt()` in `LlmInterface.cpp` to customize:
- What artifacts are included
- How they're formatted
- Additional instructions to the LLM

### Changing the LLM Model

Edit `generate_plan.py` and modify the `model` parameter:
```python
response = client.chat.completions.create(
    model="gpt-4o",  # Change to your preferred model
    ...
)
```

### Adjusting Token Limits

Modify `max_tokens` in `generate_plan.py` if you need longer plans:
```python
max_tokens=4000  # Increase if needed
```

