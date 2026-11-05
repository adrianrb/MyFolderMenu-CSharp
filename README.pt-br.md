🇺🇸 [English](README.md) | 🇧🇷 [Português (Brasil)](README.pt-br.md)

# MyFolderMenu

Um aplicativo leve e de alto desempenho para a bandeja do sistema Windows (`.NET 8` WinForms) que oferece acesso rápido às suas pastas e arquivos favoritos, sem poluir a barra de tarefas ou a área de trabalho.

---

## Principais Recursos

- **Integração com a Bandeja:** Executa discretamente na área de notificação com ícone personalizado extraído diretamente do executável.
- **Submenus sob Demanda:** Subpastas e arquivos são carregados dinamicamente (`DropDownOpening`), garantindo inicialização instantânea e baixo consumo de memória.
- **Ordenação Inteligente:** Os submenus exibem primeiro os diretórios e depois os arquivos, ambos ordenados em ordem alfabética.
- **Ícones Reais do Shell:** Arquivos e pastas utilizam os ícones autênticos do Windows e seus manipuladores registrados.
- **Ocultar Extensões:** Opção para ocultar ou exibir as extensões dos arquivos (`.txt`, `.pdf`, etc.).
- **Suporte a Temas:** Modos **Claro**, **Escuro** e **Sistema**, incluindo barras de título escuras nativas no Windows 10/11.
- **Configurações Persistentes:** Pastas e preferências salvas em `%APPDATA%\MyFolderMenu\folders.ini`, com suporte completo à exportação e importação.
- **Inicialização com o Windows:** Opção integrada na tela de configurações para iniciar com o sistema operacional.
- **Instância Única:** Garantida por mutex local (`Local\MyFolderMenu`) para evitar execuções duplicadas.

---

## Requisitos

- **Sistema Operacional:** Windows 10 ou Windows 11 (64 bits)
- **Runtime:** .NET 8 Desktop Runtime (x64)

---

## Instalação e Execução

1. Baixe ou compile o pacote da versão mais recente (`MyFolderMenu-win-x64`).
2. Extraia os arquivos para um diretório de sua preferência.
3. Execute `MyFolderMenu.exe`.
4. Clique com o botão direito no ícone da bandeja para navegar pelas pastas, adicionar novos diretórios ou abrir as **Configurações...**.

---

## Configuração

Na primeira execução, o MyFolderMenu inicializa automaticamente com as pastas **Área de Trabalho (Desktop)**, **Downloads** e **Documentos**.
Você pode personalizar suas pastas, alterar extensões e ajustar o tema pelo menu da bandeja ou editando diretamente `%APPDATA%\MyFolderMenu\folders.ini`.

---

## Licença

Copyleft / Código Aberto. Consulte o repositório para mais detalhes.
