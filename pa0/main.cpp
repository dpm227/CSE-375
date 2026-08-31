#include <iostream>
#include <map>
#include <random>

std::mutex accounts_mutex;

void transfer(std::map<int, float> &accounts, float amount)
{
    std::random_device rd;
    std::mt19937 gen(rd());

    // inclusive range [0, x]
    std::uniform_int_distribution<> dist(0, accounts.size() - 1);

    int a1 = dist(gen);
    int a2 = dist(gen);

    while (a1 == a2)
        a2 = dist(gen);

    std::lock_guard<std::mutex> lock(accounts_mutex);

    accounts[a1] -= amount;
    accounts[a2] += amount;
}

float balance(std::map<int, float> &accounts)
{
    std::lock_guard<std::mutex> lock(accounts_mutex);

    float balance = 0;

    for (int i = 0; i < 10; i++)
        balance += accounts[i];

    return balance;
}

int main()
{
    std::map<int, float> accounts;
    // accounts.insert({})

    for (int i = 0; i < 10; i++)
        accounts.insert({i, 1000});

    transfer(accounts, 100);

    for (int i = 0; i < 10; i++)
        std::cout << accounts[i] << std::endl;

    std::cout << balance(accounts) << std::endl;

    return 0;
}