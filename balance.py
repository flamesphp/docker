import sys
import urllib.request
import concurrent.futures
import random
import re
import time
import threading

# Códigos de cores ANSI
VERDE = '\033[92m'
VERMELHO = '\033[91m'
AZUL = '\033[94m'
AMARELO = '\033[93m'
RESET = '\033[0m'

evento_parada = threading.Event()

def fazer_requisicoes(worker_id, quantidade):
    tempos_ms = []
    for i in range(1, quantidade + 1):
        if evento_parada.is_set():
            break

        calc_a = random.randint(1, 10)
        calc_b = random.randint(1, 10)
        url = f"http://localhost:8080/?append={i}&calc_a={calc_a}&calc_b={calc_b}"

        try:
            inicio = time.perf_counter()
            resposta = urllib.request.urlopen(url, timeout=10)
            status_code = resposta.getcode()
            conteudo = resposta.read().decode('utf-8', errors='ignore')
            fim = time.perf_counter()

            tempo_ms = int((fim - inicio) * 1000)
            tempos_ms.append(tempo_ms)

            linhas = conteudo.splitlines()

            if linhas:
                primeira_linha = linhas[0]
                numeros_encontrados = re.findall(r'\d+', primeira_linha)

                if numeros_encontrados:
                    resultado_servidor = int(numeros_encontrados[0])
                    resultado_esperado = calc_a + calc_b

                    if status_code == 200 and resultado_servidor == resultado_esperado:
                        mensagem = f"[Worker {worker_id}] Req {i} | {calc_a} + {calc_b} | Status: {status_code} | Tempo: {tempo_ms}ms | Retorno: {resultado_servidor} | Cálculo: Correto"
                        print(f"{VERDE}{mensagem}{RESET}")
                    else:
                        mensagem = f"[Worker {worker_id}] Req {i} | {calc_a} + {calc_b} | Status: {status_code} | Tempo: {tempo_ms}ms | Retorno: {resultado_servidor} | Cálculo: Incorreto (Esperado: {resultado_esperado})"
                        print(f"{VERMELHO}{mensagem}{RESET}")
                else:
                    mensagem = f"[Worker {worker_id}] Req {i} | {calc_a} + {calc_b} | Status: {status_code} | Tempo: {tempo_ms}ms | Nenhum número na linha: {primeira_linha}"
                    print(f"{VERMELHO}{mensagem}{RESET}")
            else:
                mensagem = f"[Worker {worker_id}] Req {i} | {calc_a} + {calc_b} | Status: {status_code} | Tempo: {tempo_ms}ms | Retorno vazio"
                print(f"{VERMELHO}{mensagem}{RESET}")

        except Exception as e:
            mensagem = f"[Worker {worker_id}] Falha na requisição {i}: {type(e).__name__} ({e})"
            print(f"{VERMELHO}{mensagem}{RESET}")

    return tempos_ms

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Como usar: python3 balance.py <requisicoes_por_worker> <numero_de_workers>")
        sys.exit(1)

    try:
        reqs_por_worker = int(sys.argv[1])
        num_workers = int(sys.argv[2])
    except ValueError:
        print("Forneça números inteiros válidos para os argumentos.")
        sys.exit(1)

    print(f"Iniciando {num_workers} workers.")
    print(f"Total estimado: {reqs_por_worker * num_workers} requisições.\n")

    executor = concurrent.futures.ThreadPoolExecutor(max_workers=num_workers)
    futures = []

    tempo_inicio_global = time.perf_counter()

    for worker_id in range(1, num_workers + 1):
        futures.append(executor.submit(fazer_requisicoes, worker_id, reqs_por_worker))

    try:
        while not all(f.done() for f in futures):
            time.sleep(0.1)
    except KeyboardInterrupt:
        print(f"\n{AMARELO}Cancelamento solicitado pelo usuário. Aguardando workers finalizarem a requisição atual para gerar o relatório...{RESET}")
        evento_parada.set()

    # Coleta dos resultados
    tempos_totais = []
    for futuro in futures:
        try:
            resultados_worker = futuro.result()
            if resultados_worker:
                tempos_totais.extend(resultados_worker)
        except Exception as e:
            print(f"{VERMELHO}Erro crítico que derrubou um worker: {type(e).__name__} ({e}){RESET}")

    tempo_fim_global = time.perf_counter()
    tempo_total_segundos = tempo_fim_global - tempo_inicio_global

    # Cálculos finais
    total_realizadas = len(tempos_totais)

    print(f"\n{AZUL}========================================{RESET}")
    print(f"{AZUL}          RELATÓRIO DE EXECUÇÃO         {RESET}")
    print(f"{AZUL}========================================{RESET}")

    if total_realizadas > 0:
        media_ms = sum(tempos_totais) / total_realizadas
        rps = total_realizadas / tempo_total_segundos

        print(f"Requisições completadas: {total_realizadas}")
        print(f"Tempo total de execução: {tempo_total_segundos:.2f} segundos")
        print(f"Média de resposta:       {media_ms:.2f} ms")
        print(f"Requests Per Second:     {rps:.2f} RPS")
    else:
        print("Nenhuma requisição foi completada com sucesso para gerar métricas.")

    print(f"{AZUL}========================================{RESET}\n")