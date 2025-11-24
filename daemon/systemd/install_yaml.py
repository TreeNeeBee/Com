#!/usr/bin/env python3
"""
LightAP Service Registry Installer (YAML-based)

Purpose:
    Install systemd socket activation components based on YAML configuration
    Replaces the legacy install.sh bash script with a modern Python approach

Features:
    - Read installation config from installation_config.yaml
    - Create users and groups
    - Install systemd units, executables, and config files
    - Verify installation integrity
    - Support dry-run mode

Usage:
    # Install all components
    sudo ./install_yaml.py
    
    # Dry-run (show what would be done)
    sudo ./install_yaml.py --dry-run
    
    # Uninstall
    sudo ./install_yaml.py --uninstall
    
    # Verify installation
    sudo ./install_yaml.py --verify

Author: LightAP Architecture Team
Date: 2025-11-19
Version: 1.0
"""

import sys
import os
import argparse
import yaml
import subprocess
import shutil
from pathlib import Path
from typing import Dict, List, Optional
import pwd
import grp
import stat

class Color:
    """ANSI color codes"""
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    NC = '\033[0m'  # No Color

class Installer:
    """LightAP Registry Installer"""
    
    def __init__(self, config_file: Path, dry_run: bool = False):
        self.config_file = config_file
        self.dry_run = dry_run
        self.config: Dict = {}
        self.script_dir = Path(__file__).parent.absolute()
        
    def load_config(self):
        """Load installation configuration from YAML"""
        try:
            with open(self.config_file, 'r') as f:
                self.config = yaml.safe_load(f)
            print(f"{Color.GREEN}✅ Loaded configuration: {self.config_file}{Color.NC}")
        except Exception as e:
            print(f"{Color.RED}❌ Failed to load config: {e}{Color.NC}")
            sys.exit(1)
    
    def run_command(self, cmd: List[str], description: str = "") -> bool:
        """Run shell command"""
        if description:
            print(f"{Color.BLUE}▶ {description}{Color.NC}")
        
        cmd_str = ' '.join(cmd)
        
        if self.dry_run:
            print(f"{Color.YELLOW}[DRY-RUN] {cmd_str}{Color.NC}")
            return True
        
        try:
            result = subprocess.run(cmd, check=True, capture_output=True, text=True)
            return True
        except subprocess.CalledProcessError as e:
            print(f"{Color.RED}❌ Command failed: {cmd_str}{Color.NC}")
            print(f"   Error: {e.stderr}")
            return False
    
    def check_root(self) -> bool:
        """Check if running as root"""
        if os.geteuid() != 0 and not self.dry_run:
            print(f"{Color.RED}❌ This script must be run as root{Color.NC}")
            return False
        return True
    
    def check_systemd(self) -> bool:
        """Check systemd version"""
        try:
            result = subprocess.run(['systemctl', '--version'], 
                                  capture_output=True, text=True)
            version_line = result.stdout.split('\n')[0]
            print(f"{Color.GREEN}✅ {version_line}{Color.NC}")
            return True
        except Exception as e:
            print(f"{Color.RED}❌ systemd not found: {e}{Color.NC}")
            return False
    
    def create_users_groups(self):
        """Create system users and groups"""
        install_cfg = self.config.get('installation', {})
        
        # Create groups
        for group in install_cfg.get('groups', []):
            group_name = group['name']
            gid = group.get('gid')
            
            try:
                grp.getgrnam(group_name)
                print(f"{Color.YELLOW}  Group '{group_name}' already exists{Color.NC}")
            except KeyError:
                cmd = ['groupadd']
                if gid:
                    cmd.extend(['-g', str(gid)])
                cmd.append(group_name)
                
                self.run_command(cmd, f"Creating group: {group_name}")
        
        # Create users
        for user in install_cfg.get('users', []):
            user_name = user['name']
            
            try:
                pwd.getpwnam(user_name)
                print(f"{Color.YELLOW}  User '{user_name}' already exists{Color.NC}")
            except KeyError:
                cmd = ['useradd']
                if user.get('uid'):
                    cmd.extend(['-u', str(user['uid'])])
                if user.get('system'):
                    cmd.append('--system')
                if user.get('shell'):
                    cmd.extend(['-s', user['shell']])
                if user.get('home'):
                    cmd.extend(['-d', user['home']])
                cmd.append(user_name)
                
                self.run_command(cmd, f"Creating user: {user_name}")
    
    def create_directories(self):
        """Create runtime directories"""
        install_cfg = self.config.get('installation', {})
        
        for dir_cfg in install_cfg.get('runtime_directories', []):
            dir_path = Path(dir_cfg['path'])
            
            if not self.dry_run:
                dir_path.mkdir(parents=True, exist_ok=True)
                
                # Set permissions
                mode = int(dir_cfg.get('permissions', '0755'), 8)
                dir_path.chmod(mode)
                
                # Set owner
                owner = dir_cfg.get('owner', 'root')
                group = dir_cfg.get('group', 'root')
                
                if owner != 'root' or group != 'root':
                    shutil.chown(dir_path, owner, group)
                
                print(f"{Color.GREEN}  Created directory: {dir_path}{Color.NC}")
            else:
                print(f"{Color.YELLOW}[DRY-RUN] mkdir -p {dir_path}{Color.NC}")
    
    def install_systemd_units(self):
        """Install systemd unit files"""
        install_cfg = self.config.get('installation', {})
        systemd_dir = Path(install_cfg['paths']['systemd_unit_dir'])
        
        for unit in install_cfg.get('systemd_units', []):
            unit_name = unit['name']
            source = self.script_dir / unit['source']
            dest = systemd_dir / unit_name
            
            if not source.exists():
                print(f"{Color.RED}❌ Source file not found: {source}{Color.NC}")
                continue
            
            if not self.dry_run:
                shutil.copy2(source, dest)
                dest.chmod(0o644)
                print(f"{Color.GREEN}  Installed: {unit_name}{Color.NC}")
            else:
                print(f"{Color.YELLOW}[DRY-RUN] cp {source} {dest}{Color.NC}")
    
    def install_executables(self):
        """Install executable files"""
        install_cfg = self.config.get('installation', {})
        
        for exe in install_cfg.get('executables', []):
            source = Path(exe['source'])
            dest = Path(exe['destination'])
            
            if not source.exists():
                print(f"{Color.YELLOW}  Skipping (not built): {source}{Color.NC}")
                continue
            
            if not self.dry_run:
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, dest)
                
                # Set permissions
                mode = int(exe.get('permissions', '0755'), 8)
                dest.chmod(mode)
                
                # Set owner/group
                owner = exe.get('owner', 'root')
                group = exe.get('group', 'root')
                if owner != 'root' or group != 'root':
                    shutil.chown(dest, owner, group)
                
                print(f"{Color.GREEN}  Installed: {exe['name']}{Color.NC}")
            else:
                print(f"{Color.YELLOW}[DRY-RUN] cp {source} {dest}{Color.NC}")
    
    def install_config_files(self):
        """Install configuration files"""
        install_cfg = self.config.get('installation', {})
        
        for cfg_file in install_cfg.get('config_files', []):
            source = self.script_dir.parent.parent / cfg_file['source']
            dest = Path(cfg_file['destination'])
            
            if not source.exists():
                print(f"{Color.RED}❌ Config file not found: {source}{Color.NC}")
                continue
            
            if not self.dry_run:
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, dest)
                
                mode = int(cfg_file.get('permissions', '0644'), 8)
                dest.chmod(mode)
                
                print(f"{Color.GREEN}  Installed: {cfg_file['name']}{Color.NC}")
            else:
                print(f"{Color.YELLOW}[DRY-RUN] cp {source} {dest}{Color.NC}")
    
    def systemd_daemon_reload(self):
        """Reload systemd daemon"""
        self.run_command(['systemctl', 'daemon-reload'], 
                        "Reloading systemd daemon")
    
    def enable_start_sockets(self):
        """Enable and start socket units"""
        install_cfg = self.config.get('installation', {})
        
        for unit in install_cfg.get('systemd_units', []):
            if unit['type'] != 'socket':
                continue
            
            unit_name = unit['name']
            
            if unit.get('enabled', False):
                self.run_command(['systemctl', 'enable', unit_name],
                               f"Enabling {unit_name}")
            
            if unit.get('started', False):
                self.run_command(['systemctl', 'start', unit_name],
                               f"Starting {unit_name}")
    
    def verify_installation(self) -> bool:
        """Verify installation"""
        print(f"\n{Color.BLUE}{'='*60}{Color.NC}")
        print(f"{Color.BLUE}Verifying Installation{Color.NC}")
        print(f"{Color.BLUE}{'='*60}{Color.NC}\n")
        
        verify_cfg = self.config.get('verification', {})
        all_ok = True
        
        for check in verify_cfg.get('checks', []):
            check_name = check['name']
            description = check.get('description', check_name)
            
            print(f"{Color.BLUE}▶ {description}{Color.NC}")
            
            if check_name == 'socket_exists':
                for file_path in check.get('files', []):
                    if Path(file_path).exists():
                        print(f"{Color.GREEN}  ✅ {file_path}{Color.NC}")
                    else:
                        print(f"{Color.RED}  ❌ {file_path} not found{Color.NC}")
                        all_ok = False
            
            elif check_name == 'service_status':
                for unit in check.get('systemd_units', []):
                    result = subprocess.run(
                        ['systemctl', 'is-active', unit['name']],
                        capture_output=True, text=True
                    )
                    status = result.stdout.strip()
                    expected = unit['expected_state']
                    
                    if status == expected:
                        print(f"{Color.GREEN}  ✅ {unit['name']}: {status}{Color.NC}")
                    else:
                        print(f"{Color.RED}  ❌ {unit['name']}: {status} (expected: {expected}){Color.NC}")
                        all_ok = False
        
        return all_ok
    
    def install(self):
        """Main installation flow"""
        print(f"\n{Color.BLUE}{'='*60}{Color.NC}")
        print(f"{Color.BLUE}LightAP Service Registry Installation{Color.NC}")
        print(f"{Color.BLUE}{'='*60}{Color.NC}\n")
        
        if not self.check_root():
            return False
        
        if not self.check_systemd():
            return False
        
        self.load_config()
        
        # Execute installation steps
        print(f"\n{Color.BLUE}Creating users and groups...{Color.NC}")
        self.create_users_groups()
        
        print(f"\n{Color.BLUE}Creating directories...{Color.NC}")
        self.create_directories()
        
        print(f"\n{Color.BLUE}Installing executables...{Color.NC}")
        self.install_executables()
        
        print(f"\n{Color.BLUE}Installing configuration files...{Color.NC}")
        self.install_config_files()
        
        print(f"\n{Color.BLUE}Installing systemd units...{Color.NC}")
        self.install_systemd_units()
        
        print(f"\n{Color.BLUE}Reloading systemd...{Color.NC}")
        self.systemd_daemon_reload()
        
        print(f"\n{Color.BLUE}Enabling and starting sockets...{Color.NC}")
        self.enable_start_sockets()
        
        print(f"\n{Color.GREEN}{'='*60}{Color.NC}")
        print(f"{Color.GREEN}Installation complete!{Color.NC}")
        print(f"{Color.GREEN}{'='*60}{Color.NC}\n")
        
        # Verify
        if not self.dry_run:
            self.verify_installation()
        
        return True


def main():
    parser = argparse.ArgumentParser(
        description='LightAP Service Registry Installer (YAML-based)',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    
    parser.add_argument('--config',
                       default='installation_config.yaml',
                       help='Installation configuration file (default: installation_config.yaml)')
    
    parser.add_argument('--dry-run',
                       action='store_true',
                       help='Show what would be done without making changes')
    
    parser.add_argument('--verify',
                       action='store_true',
                       help='Verify existing installation')
    
    parser.add_argument('--uninstall',
                       action='store_true',
                       help='Uninstall components')
    
    args = parser.parse_args()
    
    config_file = Path(args.config)
    if not config_file.exists():
        print(f"{Color.RED}❌ Config file not found: {config_file}{Color.NC}")
        return 1
    
    installer = Installer(config_file, dry_run=args.dry_run)
    
    if args.verify:
        installer.load_config()
        success = installer.verify_installation()
        return 0 if success else 1
    
    if args.uninstall:
        print(f"{Color.YELLOW}Uninstallation not yet implemented{Color.NC}")
        return 1
    
    success = installer.install()
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
