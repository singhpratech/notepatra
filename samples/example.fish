#!/usr/bin/env fish
# Notepatra palette preview — synthetic; no real data

set -g APP_NAME "notepatra"
set -g APP_VERSION "0.1.84"
set -lx APP_ENV "development"

abbr -a -- ll 'ls -la'
abbr -a -- g  'git status'

function greet --description "Print a friendly greeting"
    set -l name $argv[1]
    if test -z "$name"
        set name "world"
    end
    echo "Hello, $name! Running $APP_NAME v$APP_VERSION."
end

function fish_prompt --description "Custom prompt"
    set -l last_status $status
    set_color cyan
    echo -n (whoami)'@'(hostname)
    set_color normal
    echo -n ':'
    set_color yellow
    echo -n (prompt_pwd)
    set_color normal
    if test $last_status -ne 0
        set_color red
        echo -n " [$last_status]"
        set_color normal
    end
    echo -n '> '
end

function deploy --argument-names target
    switch $target
        case staging
            echo "Deploying to staging…"
        case prod production
            echo "Deploying to production…"
        case '*'
            echo "Unknown target: $target"
            return 1
    end
end

for user in alice bob carol
    greet $user
end

set -l files (find . -type f -name '*.md' 2>/dev/null)
echo "Found "(count $files)" markdown files"

set -l now (date '+%Y-%m-%d %H:%M:%S')
echo "Now: $now"

# Command substitution + math
set -l n 5
echo "n * 2 = "(math "$n * 2")
