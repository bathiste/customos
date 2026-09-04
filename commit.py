#!/usr/bin/env python3
"""Commit to GitHub: bathiste/customos
Usage: python3 commit.py "commit message"
"""
import os, sys, base64, requests
from datetime import datetime

TOKEN = "YOUR_GITHUB_PAT"
USER = "bathiste"
REPO = "customos"
EMAIL = "229485721+bathiste@users.noreply.github.com"
MSG = sys.argv[1] if len(sys.argv) > 1 else "Update CustomOS"

def files():
    out = []
    skip = {'.git','build','__pycache__','.github'}
    for root, dirs, names in os.walk("."):
        dirs[:] = [d for d in dirs if d not in skip]
        for n in names:
            if n not in skip:
                out.append(os.path.join(root, n))
    return out

def commit(msg):
    h = {"Authorization":f"token {TOKEN}","Accept":"application/vnd.github.v3+json"}
    r = requests.get(f"https://api.github.com/repos/{USER}/{REPO}", headers=h)
    if r.status_code != 200:
        print(f"Error: {r.status_code}"); return
    branch = r.json().get("default_branch","main")
    
    sha = requests.get(f"https://api.github.com/repos/{USER}/{REPO}/git/ref/heads/{branch}", headers=h).json()["object"]["sha"]
    tree_sha = requests.get(f"https://api.github.com/repos/{USER}/{REPO}/git/commits/{sha}", headers=h).json()["tree"]["sha"]
    
    items = []
    for f in files():
        with open(f,"rb") as fp:
            c = fp.read().decode("utf-8","replace")
        items.append({"path":f.lstrip("./"),"mode":"100644","type":"blob","content":c})
        print(f"  + {f}")
    
    new_tree = requests.post(f"https://api.github.com/repos/{USER}/{REPO}/git/trees", headers=h, json={"base_tree":tree_sha,"tree":items}).json()["sha"]
    new_commit = requests.post(f"https://api.github.com/repos/{USER}/{REPO}/git/commits", headers=h, json={"message":msg,"author":{"name":USER,"email":EMAIL,"date":datetime.utcnow().isoformat()+"Z"},"parents":[sha],"tree":new_tree}).json()["sha"]
    requests.patch(f"https://api.github.com/repos/{USER}/{REPO}/git/refs/heads/{branch}", headers=h, json={"sha":new_commit})
    print(f"Done! Commit: {new_commit[:10]}")

if __name__ == "__main__":
    if TOKEN == "YOUR_GITHUB_PAT":
        print("ERROR: Edit commit.py and add your GitHub token!")
        print("Get one at: https://github.com/settings/tokens")
        sys.exit(1)
    print(f"Committing to {USER}/{REPO}...")
    print(f"Message: {MSG}\n")
    commit(MSG)
