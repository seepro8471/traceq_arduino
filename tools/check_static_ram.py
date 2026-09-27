# 실빌드 정적 RAM 상한 관문 — PlatformIO extra_scripts(post) 에서 불린다.
# 근거(6회차 AA3 실측): .data+.bss 3021B · 스택 최악 1527B · 자유 5163B → 정적이 6.6KB 를 넘으면 겹친다.
#   상한은 넉넉히 5000B. 넘으면 빌드를 실패시킨다(시뮬에는 RAM_LIMIT 이 있었는데 실빌드엔 없었다).
# ★한계: **ELF 가 다시 링크될 때만** 돈다 — 이미 최신인 빌드의 SUCCESS 는 "관문을 통과했다" 가 아니다.
#   소스가 바뀌면 반드시 재링크되므로 회귀는 잡힌다(상한을 3000 으로 낮춰 실패를 실측 확인).
Import("env")

LIMIT = 5000

def check_static_ram(source, target, env):
    import subprocess, re, os
    elf = str(target[0])
    if not os.path.exists(elf):
        return
    size = env.subst("$SIZETOOL") or "avr-size"
    try:
        out = subprocess.check_output([size, "-A", elf], universal_newlines=True)
    except Exception as e:
        print("[static-ram] 크기 확인 실패:", e)
        return
    used = 0
    for line in out.splitlines():
        m = re.match(r"^\.(data|bss)\s+(\d+)", line.strip())
        if m:
            used += int(m.group(2))
    print("[static-ram] .data+.bss = %dB / 상한 %dB" % (used, LIMIT))
    if used > LIMIT:
        print("*** 정적 RAM %dB > %dB — 스택(최악 1527B)과 겹칠 여지가 커진다. "
              "큰 정적 버퍼를 줄이거나, 근거를 다시 재고 상한을 올릴 것." % (used, LIMIT))
        env.Exit(1)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_static_ram)
