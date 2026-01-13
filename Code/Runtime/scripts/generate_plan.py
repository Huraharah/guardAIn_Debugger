#!/usr/bin/env python3
"""
LLM Interface Script for guardAIn Debug Plan Generation

This script reads a prompt file containing static analysis artifacts
and generates a GDB debug plan JSON using an LLM.

Usage:
    python generate_plan.py <prompt_file> <output_json_file>

Configuration:
    The script uses the OPENAI_API_KEY environment variable.
    Alternatively, you can modify this script to use Cursor's API or another LLM provider.
"""

import sys
import os
import json
import re
from pathlib import Path


def extract_json_from_response(response_text):
    """
    Extract JSON from LLM response, handling markdown code blocks if present.
    """
    # Try to find JSON in code blocks first
    json_match = re.search(r'```(?:json)?\s*(\{.*?\})\s*```', response_text, re.DOTALL)
    if json_match:
        return json_match.group(1)
    
    # Try to find JSON object directly
    json_match = re.search(r'\{.*\}', response_text, re.DOTALL)
    if json_match:
        return json_match.group(0)
    
    # Return the whole response if no pattern matches
    return response_text.strip()


def generate_plan_with_openai(prompt_content, api_key=None, model=None):
    """
    Generate plan using OpenAI API (which Cursor uses internally).
    
    Args:
        prompt_content: The full prompt text
        api_key: OpenAI API key (or None to use environment variable)
        model: Model name (or None to use environment variable or default)
    
    Returns:
        Generated JSON plan as string
    """
    try:
        import openai
    except ImportError:
        print("ERROR: openai library not installed. Install with: pip install openai", file=sys.stderr)
        print("\nAlternatively, you can use Cursor's API by modifying this script.", file=sys.stderr)
        sys.exit(1)
    
    if api_key is None:
        api_key = os.environ.get("OPENAI_API_KEY")
        if not api_key:
            print("ERROR: OPENAI_API_KEY environment variable not set.", file=sys.stderr)
            print("Either set OPENAI_API_KEY or modify this script to use Cursor's API.", file=sys.stderr)
            sys.exit(1)
    
    # Determine model - check environment variable first, then use provided model, then default
    if model is None:
        model = os.environ.get("LLM_MODEL", "gpt-5-mini")  # Default to cheaper model with large context
    
    client = openai.OpenAI(api_key=api_key)
    
            # Adjust max_tokens based on model context window
            # For large context models, allow more output tokens
            # Note: gpt-5-mini doesn't exist yet, user may have meant gpt-4o-mini
    max_output_tokens = 8000 if "128k" in model or "gpt-4o-mini" in model or "gpt-5-mini" in model else 4000
    
    try:
        response = client.chat.completions.create(
            model=model,
            messages=[
                {
                    "role": "system",
                    "content": "You are an expert malware analyst. Generate GDB debug plans from static analysis artifacts. Always respond with valid JSON only."
                },
                {
                    "role": "user",
                    "content": prompt_content
                }
            ],
            #temperature=0.3,  # Lower temperature for more deterministic output
            #max_tokens=max_output_tokens
        )
        
        generated_text = response.choices[0].message.content
        json_content = extract_json_from_response(generated_text)
        return json_content
    
    except Exception as e:
        print(f"ERROR: Failed to call OpenAI API: {e}", file=sys.stderr)
        sys.exit(1)


def generate_plan_with_claude(prompt_content, api_key=None, model=None):
    """
    Generate plan using Anthropic Claude API (recommended for large contexts).
    
    Args:
        prompt_content: The full prompt text
        api_key: Anthropic API key (or None to use ANTHROPIC_API_KEY environment variable)
        model: Model name (default: claude-3-5-sonnet-20241022)
    
    Returns:
        Generated JSON plan as string
    """
    try:
        from anthropic import Anthropic
    except ImportError:
        print("ERROR: anthropic library not installed. Install with: pip install anthropic", file=sys.stderr)
        sys.exit(1)
    
    if api_key is None:
        api_key = os.environ.get("ANTHROPIC_API_KEY")
        if not api_key:
            print("ERROR: ANTHROPIC_API_KEY environment variable not set.", file=sys.stderr)
            print("Get your API key from: https://console.anthropic.com/", file=sys.stderr)
            sys.exit(1)
    
    if model is None:
        model = os.environ.get("LLM_MODEL", "claude-3-5-sonnet-20241022")
    
    client = Anthropic(api_key=api_key)
    
    try:
        response = client.messages.create(
            model=model,
            max_tokens=8192,  # Claude supports up to 8192 output tokens
            system="You are an expert malware analyst. Generate GDB debug plans from static analysis artifacts. Always respond with valid JSON only.",
            messages=[
                {
                    "role": "user",
                    "content": prompt_content
                }
            ],
            temperature=0.3
        )
        
        generated_text = response.content[0].text
        json_content = extract_json_from_response(generated_text)
        return json_content
    
    except Exception as e:
        print(f"ERROR: Failed to call Claude API: {e}", file=sys.stderr)
        sys.exit(1)


def generate_plan_with_cursor_api(prompt_content):
    """
    Generate plan using Cursor's API (if available).
    
    NOTE: This is a placeholder. Cursor's API access requires special setup.
    You may need to use Cursor's MCP server or modify this to work with your setup.
    """
    print("ERROR: Cursor API integration not yet implemented.", file=sys.stderr)
    print("Please use OpenAI API or implement Cursor API integration.", file=sys.stderr)
    sys.exit(1)


def main():
    if len(sys.argv) < 3 or len(sys.argv) > 4:
        print("Usage: python generate_plan.py <prompt_file> <output_json_file> [api_key]", file=sys.stderr)
        sys.exit(1)
    
    prompt_file = Path(sys.argv[1])
    output_file = Path(sys.argv[2])
    api_key_override = sys.argv[3] if len(sys.argv) == 4 else None
    
    if not prompt_file.exists():
        print(f"ERROR: Prompt file not found: {prompt_file}", file=sys.stderr)
        sys.exit(1)
    
    # Read prompt
    try:
        with open(prompt_file, 'r', encoding='utf-8') as f:
            prompt_content = f.read()
    except Exception as e:
        print(f"ERROR: Failed to read prompt file: {e}", file=sys.stderr)
        sys.exit(1)
    
    print(f"Reading prompt from: {prompt_file}", file=sys.stderr)
    print(f"Prompt size: {len(prompt_content)} characters", file=sys.stderr)
    
    # Determine which API to use
    llm_provider = os.environ.get("LLM_PROVIDER", "openai").lower()  # Default to OpenAI
    use_cursor = os.environ.get("USE_CURSOR_API", "").lower() == "true"
    
    # Generate plan
    if use_cursor:
        print("Using Cursor API (if available)...", file=sys.stderr)
        json_content = generate_plan_with_cursor_api(prompt_content)
    elif llm_provider == "claude" or llm_provider == "anthropic":
        print(f"Using Claude API...", file=sys.stderr)
        # For Claude, use ANTHROPIC_API_KEY, but allow override if passed
        claude_key = api_key_override if api_key_override else None
        json_content = generate_plan_with_claude(prompt_content, api_key=claude_key)
    else:
        model = os.environ.get("LLM_MODEL", None)  # Allow model selection via env var
        print(f"Using OpenAI API (model: {model or 'GPT 5 Mini (default)'})...", file=sys.stderr)
        # Use command-line API key if provided, otherwise use environment variable
        json_content = generate_plan_with_openai(prompt_content, api_key=api_key_override, model=model)
    
    # Validate JSON
    try:
        parsed_json = json.loads(json_content)
    except json.JSONDecodeError as e:
        print(f"ERROR: Generated content is not valid JSON: {e}", file=sys.stderr)
        print(f"Generated content (first 500 chars):\n{json_content[:500]}", file=sys.stderr)
        sys.exit(1)
    
    # Write output
    try:
        output_file.parent.mkdir(parents=True, exist_ok=True)
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(parsed_json, f, indent=2, ensure_ascii=False)
        print(f"Successfully generated plan: {output_file}", file=sys.stderr)
    except Exception as e:
        print(f"ERROR: Failed to write output file: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()

