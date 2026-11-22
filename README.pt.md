# KarmaTV PostFX para rFactor 2

[English](README.md) · [Italiano](README.it.md) · [Español](README.es.md) · [Français](README.fr.md) · [Deutsch](README.de.md) · **Português**

Efeitos gráficos para o rFactor 2 com um menu dentro do jogo: perfis prontos, ajustes avançados, automações guiadas pela telemetria (chuva, replays, câmera) e atualizações automáticas. É um add-on do [ReShade](https://reshade.me); o instalador já deixa o ReShade pronto. O menu está em inglês, italiano, espanhol, francês, alemão e português.

## Instalação

1. Baixe `KarmaTV-PostFX-Setup-x.y.z.exe` da [versão mais recente](https://github.com/KarmaTV80/karmatv-postfx-rf2/releases/latest).
2. Feche o rFactor 2 e execute o instalador: ele encontra o jogo nas suas bibliotecas da Steam.
3. Abra o rF2, entre na pista e pressione **Delete** para abrir o menu.

O Windows pode mostrar "O Windows protegeu o computador" porque o instalador não tem assinatura digital: escolha **Mais informações** e depois **Executar assim mesmo**.

As automações precisam do [rF2SharedMemoryMapPlugin](https://github.com/TheIronWolfModding/rF2SharedMemoryMapPlugin), ativado nas opções de Plugins do rF2. Os efeitos funcionam sem ele.

Para desinstalar: **Configurações do Windows > Aplicativos > Aplicativos instalados > KarmaTV PostFX for rFactor 2**. Seus perfis são mantidos, a menos que você escolha excluí-los.

## Perfis

| Perfil | Para quê |
|---|---|
| rF2 Original | Sem efeitos: os gráficos do jogo |
| Karma FX | Pilotagem no monitor |
| Replay Karma FX | Replays e transmissão: profundidade de campo, motion blur. Ativa sozinho nos replays |
| VR Karma FX | Óculos VR, com efeitos adequados para VR |

Os perfis fixos podem ser alterados pelo botão **Avançado**, mas as alterações só são salvas como novo perfil. **Usar ao iniciar** escolhe o perfil carregado quando o jogo abre.

## Teclas

| Tecla | Ação |
|---|---|
| Delete | Abre e fecha o menu |
| + | Efeitos liga/desliga |
| , e . | Perfil anterior / próximo |
| - | Comparação A/B (à esquerda a imagem original) |

Altere-as no menu com **Personalizar teclas**, inclusive com os botões do volante. O idioma muda pelo botão no cabeçalho do menu.

## Atualizações

Ao iniciar o jogo, o add-on verifica no GitHub se há uma versão nova. Se houver, o menu mostra **Atualizar**: o instalador é baixado, conferido com sua assinatura SHA-256 e instalado quando você fecha o rFactor 2. A verificação envia ao GitHub apenas o nome e a versão do programa.

Para compilar ou ajudar nas traduções, veja a seção *Development* do [README em inglês](README.md#development).

## Licença

Gratuito, todos os direitos reservados: veja [LICENSE.txt](LICENSE.txt). O ReShade é distribuído sob a licença BSD-3-Clause e a fonte Barlow Condensed sob a SIL Open Font License.
