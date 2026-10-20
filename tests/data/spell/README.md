# Spelling test texts

Real typing mistakes, for measuring how the spell checker finds and corrects
them. Each `NN.<language>.orig.txt` is a text as published, and
`NN.<language>.typed.txt` the same text typed again by hand:

- `01.en`: the start of the English Wikipedia article "Ubuntu", typed fast
  and carelessly (swapped, missing and extra letters, neighbouring keys);
- `01.fr`: the start of the French Wikipedia article "Ubuntu", typed mostly
  without accents (`developpee` for `développée`);
- `01.de`: the start of the German Wikipedia article "Ubuntu", typed without
  umlauts (`geschatzt` for `geschätzt`) and with typing mistakes, also in
  compound words (`Betriebsystem`);
- `01.es`: the start of the Spanish Wikipedia article "Ubuntu", typed without
  accents (`distribucion` for `distribución`) and with typing mistakes.

`test_spell_texts` pairs the words of each typed text with the published
ones and checks the suggestions.

Polish material is plentiful and kept elsewhere; these add other languages.

The original texts come from Wikipedia and are licensed under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
