# mniAM-playerbot

### system priorytetów
1. **Ucieczka od zagrożeń** - omijanie silniejszych graczy w promieniu 100+25+HP pikseli
2. **Unikanie iskier** - ucieczka od iskier w promieniu 20+25+HP pikseli  
3. **Zbieranie jedzenia** - szukanie najbliższych tranzystorów z HP > 0
4. **Polowanie** - atakowanie słabszych graczy

### mechaniki
- **System punktacji** - ocena celów na podstawie HP i odległości z uwzględnieniem skali mapy
- **Detekcja kleju na ścieżce** - 20x kara za cele blokowane przez klej (promień 100px)
- **Omijanie iskier podczas ruchu**
- **Normalizacja kątów**

### algorytmy
- **Ucieczka prostopadła** - ruch prostopadły do kierunku zagrożenia
- **Dynamiczne unikanie** - korekta kąta o ±90° przy wykryciu iskry na trajektorii  

### Promienie detekcji
- **Niebezpieczni gracze**: `100 + 25 + myHP` pikseli
- **Iskry**: `20 + 25 + myHP` pikseli  
- **Unikanie iskier**: `50 + 25 + myHP` pikseli
- **Klej**: `100` pikseli (stały promień)

- **Nazwa**: "sAMobujca"
- **Wiadomość powitalna**: "Będzie magik i to za dwa lata"
- **Wiadomość końcowa**: "GG WP!"