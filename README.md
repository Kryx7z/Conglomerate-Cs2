<p align="center">
    <img src="images/logo.png">
  </a>
</p> 
<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-8B5CF6?style=for-the-badge&logo=c%2B%2B&logoColor=white">

  <img src="https://img.shields.io/badge/Visual_Studio-25252D?style=for-the-badge&logo=visual%20studio&logoColor=8B5CF6">

  <img src="https://img.shields.io/badge/Windows-8B5CF6?style=for-the-badge&logo=windows&logoColor=white">

  <img src="https://img.shields.io/badge/license-MIT-25252D?style=for-the-badge&logoColor=8B5CF6">

  <img src="https://img.shields.io/badge/CS2-8B5CF6?style=for-the-badge&logo=counter-strike&logoColor=white">
</p>

---
### Conglomerate is a free C++-based internal for Counter-Strike 2. Based on <a href="https://github.com/TempleDevelopment/TempleWare-Public">Templeware-Public.</a>
---

### How to Launch Conglomerate

1. **Build the DLL:**
   - Open the project in Visual Studio.
   - Set the build configuration to **Release x64**.
   - Build the solution (`Build` > `Build Solution`).

2. **Inject the DLL:**
   - Find the `Conglomerate.dll` in the `\Release\x64\` folder.
   - Use a DLL injector (e.g., [Extreme Injector](https://github.com/master131/ExtremeInjector/releases)) to inject `Conglomerate.dll` into `cs2.exe`.

3. **Play CS2:**
   - Add `-allow_third_party_software` to the CS2 launch options ([Tutorial](https://hackvshack.net/threads/how-to-run-cs2-with-launch-arguments-e-g-insecure-or-allow_third_party_software.7013/)).
   - Launch `Counter-Strike 2` and start using Conglomerate.
   - Menu Bind `INS`

---

## FAQ

**Is this detected?**
   - Yes. Due to Conglomerate being open source and using detected methods for hooking, it is detected..

**Why doesn’t [FEATURE] work?**
   - The source code is probably outdated, check patterns and offsets used by that feature and update them using [cs2 sdk](https://cs2-sdk.com) and [a2x dumper](https://github.com/a2x/cs2-dumper).

**Can you help me with making my own cheat or fixing it?**
   - No.



## Showcase
<p align="center">
    <img src="images/image.png">
  </a>
</p> 


---

<p align="center">
  <b>Why conglomerate?</b>

<p>Conglomerate its a <a href="https://github.com/TempleDevelopment/TempleWare-Public">TempleWare-Based</a> cheat, the original Templeware source code was beyond broken so a lot of things had to be fixed. This was possible thanks to a collection of AI source code, <a href="https://www.unknowncheats.me/forum/counter-strike-2-a/757633-updated-templeware-internal.html">UnknownCheats fixes</a>, Own source code and other leaked cheats source codes, making this cheat as the name indicates, a conglomerate of slop (the original name was Slopware).
