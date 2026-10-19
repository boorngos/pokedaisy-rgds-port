"""Copy the aarch64 PokeDaisy build onto the RG DS and start the demo."""
import os
import time

import paramiko

HOST = "192.168.1.88"
USER = "root"
PASSWORD = "root"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
REMOTE = "/mnt/mmc/pokedaisy"


def run(ssh, cmd, timeout=40):
    _, o, e = ssh.exec_command(cmd, timeout=timeout)
    out = o.read().decode("utf-8", "replace")
    err = e.read().decode("utf-8", "replace")
    st = o.channel.recv_exit_status()
    print(f"$ {cmd}\n{out}{err}[exit {st}]")
    return st, out, err


def put(sftp, local, remote):
    data = open(local, "rb").read()
    with sftp.file(remote, "wb") as f:
        f.write(data)
    print("put", remote, len(data))


def main():
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, username=USER, password=PASSWORD, timeout=12, allow_agent=False, look_for_keys=False)
    run(c, f"mkdir -p {REMOTE}/share/pokedaisy/demo {REMOTE}/roms {REMOTE}/saves")
    sftp = c.open_sftp()
    put(sftp, os.path.join(ROOT, "build-aarch64", "pokedaisy"), REMOTE + "/pokedaisy.new")
    for name, src in (
        ("PokeDaisy.sh", os.path.join(ROOT, "ports", "PokeDaisy.sh")),
        ("run-on-simpleos.sh", os.path.join(ROOT, "scripts", "run-on-simpleos.sh")),
    ):
        data = open(src, "rb").read().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
        with sftp.file(REMOTE + "/" + name, "wb") as f:
            f.write(data)
        print("put", REMOTE + "/" + name, len(data))
    readme = os.path.join(ROOT, "share", "pokedaisy", "demo", "README.md")
    if os.path.isfile(readme):
        put(sftp, readme, REMOTE + "/share/pokedaisy/demo/README.md")
    sftp.close()
    run(
        c,
        "mv -f /mnt/mmc/pokedaisy/pokedaisy.new /mnt/mmc/pokedaisy/pokedaisy; "
        "chmod 755 /mnt/mmc/pokedaisy/pokedaisy /mnt/mmc/pokedaisy/PokeDaisy.sh /mnt/mmc/pokedaisy/run-on-simpleos.sh; "
        "ldd /mnt/mmc/pokedaisy/pokedaisy; "
        "file /mnt/mmc/pokedaisy/pokedaisy 2>/dev/null || true",
    )
    # Freeze the DSi home so its windows stop covering the new ones.
    # Do not kill the loop: its exit path powers the handheld off.
    run(
        c,
        "killall pokedaisy 2>/dev/null; "
        "setsid /mnt/mmc/pokedaisy/run-on-simpleos.sh "
        ">/tmp/pokedaisy.log 2>&1 </dev/null & echo $!; sleep 2; "
        "pidof pokedaisy; echo ---LOG---; tail -n 40 /tmp/pokedaisy.log",
        timeout=20,
    )


if __name__ == "__main__":
    main()
