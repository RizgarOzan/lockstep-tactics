# MSVC'de çalışıyordu: üç derleyiciden yalnız ikisinde görünen senkron hatası

*Rızgar Ozan · 2026-10-12 · [English](2026-10-12-worked-on-msvc-en.md)*

[lockstep-tactics](https://github.com/RizgarOzan/lockstep-tactics) küçük bir
C++20 taktik simülasyonu ve tek bir sözü var: aynı girdiler her derleyicide,
her tick'te aynı durum hash'ini verir. Lockstep çok oyunculu oyunlar da
tekrar kayıtları da bu sözün üstünde durur. Söylemesi kolay, kontrol etmesi
zor. O yüzden repo bunu her push'ta kontrol ediyor: bin tohumlu maç Windows'ta
MSVC ile, Linux'ta WebAssembly ile ayrı ayrı oynanıyor ve iki hash listesi
karşılaştırılıyor.

Kontrolün birkaç yüz kez geçmesi bana hiç başarısız olup olamayacağını
söylemiyordu. Hiçbir yerde kırılmayan kural, batıl inanç gibi görünür. Ben de
simülasyonu bilerek iki kez yanlış derleyip saydım.

## Birinci kuralı kırmak: birimler `unordered_map` sırasıyla hareket etsin

Simülasyon birimleri kimliğe göre sıralı bir `std::vector`'de tutuyor ve o
sırayla hareket ettiriyor. Bozuk derleme (`-DLOCKSTEP_RISKY=unordered`) onları
bir `std::unordered_map<uint32_t, Unit*>`'e koyup onun üstünde dolaşıyor.
Sıra, isabet zarı için hangi birimin hangi rastgele sayıyı alacağına karar
verdiği için farklı sıra farklı maç demek.

600 tick'lik 1.000 maçta, normal derlemeden farklı biten maç sayısı:

| Derleyici | farklı biten maç |
|---|---|
| MSVC 19.50 x64 | **0** |
| gcc 16 (libstdc++) | 881 |
| Emscripten 6 / WebAssembly (libc++) | 860 |

gcc ile WebAssembly *birbirinden* de 249 maçta ayrışıyor.

İlginç sayı sıfır. MSVC'nin `unordered_map`'i elemanları ekleme sırasında bir
listede tutuyor; altı küçük anahtar ve yeniden hash'leme olmayınca dolaşma
tesadüfen kimlik sırasında çıkıyor, yani tam olarak güvenli sıra. Hata, Windows
geliştiricilerinin çoğunun test ettiği derleyicide görünmez. Bütün birim
testleri geçer. Bütün yerel tekrarlar tutar. Yayınla; ilk Linux ya da tarayıcı
istemcisi maçların %86'sında ayrışır.

Derleyiciler arası kontrolün, koşular arası kontrolden farkı bu: "bende
çalışıyor" burada kelimesi kelimesine doğruydu.

## İkinci kuralı kırmak: hareket `float` ile

Simülasyon her yerde Q16.16 sabit nokta kullanıyor; reponun 01 numaralı notu
derleyicilerin float işlemlerini birleştirebileceğini ya da sırasını
değiştirebileceğini söylüyor. İkinci bozuk derleme (`-DLOCKSTEP_RISKY=float`)
birimleri hedefe giden doğru boyunca `float`, karekök ve bölmeyle yürütüp
sonra sabit noktaya geri çeviriyor.

| | farklı biten maç |
|---|---|
| MSVC, gcc, WebAssembly, herhangi iki çift | **0** / 1000 |

Bunun kırılmasını bekliyordum, kırılmadı. Üç hedef de aynı yuvarlamayla IEEE
skaler float kullanıyor, `sqrt` doğru yuvarlanıyor ve ISO C++ kipinde hiçbiri
`a*b + c`'yi tek bir fused multiply-add'e birleştirmiyor. Bu üç derleyicide,
bugün, float deterministikti.

Sonra tek bir bayrak:

| | farklı biten maç |
|---|---|
| gcc `-mfma -ffp-contract=fast` vs düz gcc, float derlemesi | **12** / 1000 |
| aynı bayraklar, sabit nokta derlemesi | 0 / 1000 |

Birleştirmeye izin verilince nesne dosyasına üç FMA komutu giriyor (bayraksız
sıfır) ve on iki maç farklı bitiyor. Aynı bayraklarla derlenen sabit nokta
sürümü hiç kıpırdamıyor: bayrağın dokunacağı bir float işlemi yok. Yani "float
yok" kuralı bu derleyiciler için değil; henüz eklenmemiş derleme bayrağı,
derleyici güncellemesi ya da ARM hedefi için var, ve sabit nokta o günleri
olaysız geçiren şey.

## Çıkardıklarım

- Hiç başarısız olmamış bir determinizm kontrolü kanıtlanmamıştır. Kodu
  bilerek boz, kontrolün neyi yakaladığını say; repo iki bozuk derlemeyi de bir
  CMake seçeneği arkasında taşıyor, isteyen sayıları tekrar üretebilir.
- "MSVC'de çalışıyor" ile "gcc'de çalışıyor" ayrı iddialar. Konteyner hatası
  birinde görünmezdi, diğer ikisinde gürültülüydü.
- Float doğası gereği deterministik olmayan bir şey değil. İki derleme
  birleştirme, hassasiyet ya da kütüphane fonksiyonunda ayrıştığı an öyle
  oluyor, ve o anın ne zaman geleceğini sen kontrol etmiyorsun. Sabit nokta
  soruyu ortadan kaldırıyor.

Yukarıdakilerin hepsi repodan tekrar üretilebilir: `cmake -S . -B build-risky
-DLOCKSTEP_RISKY=unordered` (ya da `float`), `replay 1000 600`, normal
derlemeyle karşılaştır. Her kuralın gerekçesi
[`docs/notes/`](https://github.com/RizgarOzan/lockstep-tactics/tree/main/docs/notes)
altında; 06 numaralı not bu yazının uzun hâli.
