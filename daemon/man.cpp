/*
 * @Author: Daokuan.deng
 * @Date: 2023-09-03 22:47:29
 * @LastEditors: Daokuan.deng
 * @LastEditTime: 2023-09-03 22:47:31
 * @Description: 
 */
#include "ServiceDiscoveryContextManagerAdapter.h"

int main() {
    sdbus::IConnection::Options options;
    auto connection = sdbus::createConnection(options);

    DBusBridgeAdapter dbusAdapter(connection);
    ContextManager contextManager;
    ServiceDiscoveryContextManagerAdapter adapter(dbusAdapter, contextManager);

    adapter.StartDiscovery();

    // Print the context after service discovery
    std::cout << "Context after service discovery: " << contextManager.GetContext() << std::endl;

    return 0;
}
