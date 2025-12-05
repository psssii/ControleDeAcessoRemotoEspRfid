# 🔐 Sistema de Controle de Acesso (SCA) - IoT

![Status](https://img.shields.io/badge/Status-Desenvolvimento-yellow?style=flat-square)
![Stack](https://img.shields.io/badge/IoT-ESP32-blue?style=flat-square)

Sistema completo de gerenciamento de acesso a salas de aula utilizando tecnologia RFID, comunicação MQTT em tempo real e arquitetura moderna de microsserviços.

---

## 📋 Índice

- [Visão Geral](#-visão-geral)
- [Tecnologias Utilizadas](#-tecnologias-utilizadas)
- [Pré-requisitos](#-pré-requisitos)
- [Instalação e Configuração](#-instalação-e-configuração)
    1. [Infraestrutura (Docker & EMQX)](#1-infraestrutura-docker--emqx)
    2. [Backend (API)](#2-backend-api)
    3. [Frontend (Web)](#3-frontend-web)
    4. [Configuração do Hardware (ESP32)](#4-configuração-do-hardware-esp32)
- [Primeiro Acesso (Criar Admin)](#-primeiro-acesso-criar-admin)
- [Guia de Uso: Hardware](#-guia-de-uso-hardware)
- [Guia de Uso: Web & Gestão](#-guia-de-uso-web--gestão)
- [Contingência (Modo Offline)](#-contingência-modo-offline)
- [Solução de Problemas](#-solução-de-problemas)

---

## 🔭 Visão Geral

O sistema permite o controle de frequência e acesso físico. Um dispositivo ESP32 na porta valida cartões RFID contra um banco de dados central. O sistema web permite monitoramento em tempo real, reservas de salas e cadastro remoto de novos cartões.

**Funcionalidades:**

* ✅ Monitoramento em Tempo Real (Via WebSocket/MQTT).
* ✅ Validação de Entrada/Saída.
* ✅ Modo de Cadastro Remoto (Acionado pelo Site).
* ✅ Botão de Pânico/Reset Físico para liberação de sala sem rede.

---

## 🛠 Tecnologias Utilizadas

### Software
* **Backend:** NestJS (Node.js), Prisma ORM.
* **Frontend:** React, Vite, Bootstrap.
* **Banco de Dados:** PostgreSQL.
* **Mensageria (IoT):** MQTT (Broker EMQX).
* **Infraestrutura:** Docker & Docker Compose.

### Hardware
* **Microcontrolador:** ESP32.
* **Leitor:** RFID-RC522.
* **Interface:** Display LCD 16x2 (I2C).
* **Atuadores:** LEDs (Verde, Vermelho, Branco, Amarelo) e Botão Físico.

---

## 📦 Pré-requisitos

Antes de iniciar, certifique-se de ter instalado:

* [Node.js (LTS)](https://nodejs.org/)
* [Docker Desktop](https://www.docker.com/products/docker-desktop)
* **PNPM** (`npm install -g pnpm`)
* [Arduino IDE](https://www.arduino.cc/en/software)

---

## 🚀 Instalação e Configuração

### 1. Infraestrutura (Docker & EMQX)
Este passo fará o download automático das imagens do Banco de Dados e do Broker MQTT (EMQX).

```bash
cd esp32-api
docker-compose up -d
```
Verifique: Execute docker ps. Os containers esp32-postgres e esp32-emqx devem estar com status Up.

Como abrir o EMQX: O Broker MQTT possui um painel visual para monitoramento.

Acesse: http://localhost:18083

Login Padrão: admin

Senha Padrão: public

2. Backend (API)
Instale as dependências e configure o banco de dados.

```bash

cd esp32-api
pnpm install

# Sincronizar o banco de dados com o código
npx prisma db push

# Iniciar o servidor
pnpm run start:dev
```
O servidor iniciará em: http://localhost:3000

3. Frontend (Web)
Em um novo terminal:

```bash

cd esp32-web
pnpm install
pnpm run dev
```
O site estará acessível em: http://localhost:5173

4. Configuração do Hardware (ESP32)
Pinagem (Wiring):

RFID: SDA: GPIO 15 | SCK: 18 | MOSI: 23 | MISO: 19 | RST: 27

LCD: SDA: GPIO 21 | SCL: 22

LEDs: Verde (2), Vermelho (4), Branco (12), Amarelo (13)

Upload do Código:

Abra o arquivo .ino na Arduino IDE.

Instale as bibliotecas: PubSubClient, ArduinoJson, MFRC522, LiquidCrystal I2C.

Importante: Edite as linhas de configuração no topo do código:

C++

const char* WIFI_SSID     = "SEU_WIFI";
const char* WIFI_PASSWORD = "SUA_SENHA";
const char* MQTT_HOST     = "192.168.X.X"; // IP do computador rodando o Docker
Faça o upload para a placa.

Nota de Firewall: No Windows, pode ser necessário liberar a porta 1883 no Firewall para que o ESP32 consiga conectar.

## 👤 Primeiro Acesso (Criar Admin)
Como o banco de dados inicia vazio, você precisa criar o primeiro Administrador.

Com a API rodando, execute em um terminal:

```bash

npx prisma studio
```
Abra o link que aparecerá (geralmente http://localhost:5555).

Crie um registro na tabela Teacher.

Marque a opção (flag) "Admin" como verdadeira (true).

## 📖 Guia de Uso: Hardware
Estados do LED

🟢 Verde: Sala Livre.

🔴 Vermelho: Sala Ocupada.

⚪ Branco: Modo de Cadastro (Aproxime um cartão novo).

🟡 Amarelo: Erro de conexão ou leitura.

## 💻 Guia de Uso: Web & Gestão
🏢 Área Administrativa (Gestão)
1. Criar Salas:

Acesse o menu Salas.

Clique no botão + (Criar).

Defina o nome da sala. Nota: O ID gerado deve ser configurado no código do ESP32.

2. Criar Professores (Usuários):

Acesse o menu Professores.

Cadastre o Nome, Protocolo (Login) e Senha.

Defina se o usuário terá permissão de Administrador.

Estes dados serão usados pelo professor para logar no site.

3. Cadastrar Cartão RFID:

No menu Salas, clique em "Ativar modo de cadastro".

O LED Branco do ESP32 acenderá. Aproxime o cartão virgem.

Vá ao menu Cartões, localize o novo cartão e vincule-o ao professor criado anteriormente.

4. Liberar Sala (Reset Remoto):

Se um professor esquecer de registrar saída, clique em "Liberar Sala" no painel.

O sistema registrará a saída forçada e o LED voltará para Verde.

## 📅 Portal do Professor (Reservas)
Os professores podem acessar o sistema para garantir o uso de uma sala com antecedência.

Acesse o site com seu Protocolo e Senha.

Vá até o menu Reservas.

Selecione a Sala desejada e o Horário.

Confirme a reserva.

Ao chegar na sala, passe o cartão para confirmar a presença.

## 🚨 Contingência (Modo Offline)
O sistema possui mecanismos para funcionar mesmo se a rede cair.

Botão de Saída (Físico/Serial): Se houver falha de rede e a sala estiver travada como "Ocupada":

O professor/administrador pode acionar o comando de Reset físico (ou enviar R via Serial).

A sala ficará LIVRE (Verde) imediatamente para uso local.

O dispositivo armazenará a informação e enviará a notificação de saída para o servidor assim que a conexão for restabelecida, mantendo a integridade dos dados.

## 🐛 Solução de Problemas
Erro P1001 (API): O Docker do banco de dados está parado. Rode docker-compose up -d.

ESP32 não conecta: Verifique se o IP do computador mudou (ipconfig) e atualize no código Arduino.

Site não carrega dados: Verifique se a API (Terminal 2) está rodando.
