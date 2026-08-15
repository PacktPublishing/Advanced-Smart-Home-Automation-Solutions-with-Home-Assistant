# Chapter 2 – Prerequisites and Basic Command Reference

## Overview

This guide provides the minimum setup and command-line knowledge required to complete the examples presented in **Chapter 2 – Mastering YAML for Complex Home Assistant Configurations**.

The chapter assumes you already have a working Home Assistant installation. This document is not intended to teach Linux or Git in depth. Instead, it serves as a quick reference for the tools and commands used throughout the chapter.

---

# Required Software

Before starting, make sure you have the following installed and configured:

- Home Assistant (Operating System or Container)
- Visual Studio Code (VS Code) or the Home Assistant File Editor
- Home Assistant SSH & Web Terminal Add-on (or SSH access to your server)
- A GitHub account
- Git installed on your computer (if using local repositories)

---

# Verifying Your Environment

Before continuing, verify that you can:

- [ ] Edit files using VS Code or the Home Assistant File Editor.
- [ ] Connect to the Home Assistant SSH terminal.
- [ ] Create a repository in GitHub.
- [ ] Execute basic Linux commands.
- [ ] Execute basic Git commands.

---

# Home Assistant Configuration Folder

Most examples in this chapter assume you are working inside the Home Assistant configuration directory.

Typical location:

```text
/config
```

Examples:

```bash
cd /config
```

or

```bash
cd /config/homeassistant
```

depending on your installation.

---

# Basic Linux Commands

The following Linux commands are used throughout the chapter.

## pwd

Displays the current working directory.

```bash
pwd
```

Example output:

```text
/config
```

---

## ls

Lists files and folders.

```bash
ls
```

---

## ls -la

Lists all files, including hidden files.

```bash
ls -la
```

---

## cd

Changes the current directory.

```bash
cd /config
```

Return to your home directory:

```bash
cd ~
```

Move up one level:

```bash
cd ..
```

---

## mkdir

Creates a new directory.

```bash
mkdir backups
```

---

## cp

Copies files.

```bash
cp configuration.yaml configuration_backup.yaml
```

---

## mv

Moves or renames files.

Rename a file:

```bash
mv old.yaml new.yaml
```

Move a file:

```bash
mv test.yaml /config/packages
```

---

## rm

Deletes files.

```bash
rm test.yaml
```

> **Warning:** Files deleted with `rm` cannot be recovered easily.

---

## cat

Displays the contents of a file.

```bash
cat configuration.yaml
```

---

## nano

Opens a file using the Nano text editor.

```bash
nano configuration.yaml
```

Useful shortcuts:

| Shortcut | Description |
|----------|-------------|
| Ctrl + O | Save the file |
| Ctrl + X | Exit Nano |
| Ctrl + K | Cut the current line |
| Ctrl + U | Paste the cut line |

---

## grep

Searches for text inside files.

```bash
grep mqtt configuration.yaml
```

---

## find

Searches for files.

```bash
find /config -name "*.yaml"
```

---

# Basic Git Commands

The chapter uses Git to manage configuration changes.

## git init

Creates a new Git repository.

```bash
git init
```

---

## git status

Displays the current status of the repository.

```bash
git status
```

---

## git add

Stages files before committing.

Stage a single file:

```bash
git add configuration.yaml
```

Stage all modified files:

```bash
git add .
```

---

## git commit

Creates a snapshot of your repository.

```bash
git commit -m "Initial Home Assistant configuration"
```

---

## git branch

Lists all local branches.

```bash
git branch
```

---

## Rename the Default Branch

Older Git versions create the default branch as **master**.

Rename it to **main** using:

```bash
git branch -m master main
```

---

## git remote add

Connects your local repository to GitHub.

```bash
git remote add origin https://github.com/yourusername/homeassistant-config.git
```

---

## git push

Uploads your repository to GitHub.

First push:

```bash
git push -u origin main
```

Subsequent pushes:

```bash
git push
```

---

## git pull

Downloads changes from GitHub.

```bash
git pull
```

---

## git log

Displays commit history.

```bash
git log
```

Compact version:

```bash
git log --oneline
```

---

## git diff

Displays changes that have not yet been committed.

```bash
git diff
```

---

# Recommended Git Workflow

The recommended workflow throughout this chapter is:

1. Modify your YAML files.
2. Validate the configuration in Home Assistant.
3. Test the changes.
4. If everything works correctly:
   1. Run `git status`.
   2. Run `git add .`.
   3. Commit the changes.
   4. Push the repository to GitHub.

Following this workflow allows you to safely experiment with your configuration and quickly return to a previous working version if needed.

---

# Helpful Home Assistant Commands

Restart Home Assistant:

```bash
ha core restart
```

Display Home Assistant status:

```bash
ha core info
```

Display Supervisor information:

```bash
ha supervisor info
```

---

# Additional Resources

- Home Assistant Documentation  
  https://www.home-assistant.io/docs/

- Home Assistant Developer Documentation  
  https://developers.home-assistant.io/

- Git Documentation  
  https://git-scm.com/doc

- GitHub Documentation  
  https://docs.github.com/

---

# Summary

You are now ready to work through the examples in Chapter 2. The commands presented in this guide provide the basic command-line skills needed to edit YAML files, manage your Home Assistant configuration, and safely track changes using Git and GitHub.