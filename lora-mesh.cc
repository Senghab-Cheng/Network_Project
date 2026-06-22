
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-static-routing.h"

#include "../graph.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LoraMesh");

int main(int argc, char *argv[]) {
    // 1. Create 5 nodes
    NodeContainer nodes;
    nodes.Create(NUM_NODES);
    std::cout << "\n=== Created " << NUM_NODES << " nodes ===" << std::endl;
    
    // 2. Set up mobility for NetAnim
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    
    // Position nodes
    Ptr<ConstantPositionMobilityModel> pos0 = nodes.Get(0)->GetObject<ConstantPositionMobilityModel>();
    pos0->SetPosition(Vector(100, 100, 0));
    Ptr<ConstantPositionMobilityModel> pos1 = nodes.Get(1)->GetObject<ConstantPositionMobilityModel>();
    pos1->SetPosition(Vector(200, 50, 0));
    Ptr<ConstantPositionMobilityModel> pos2 = nodes.Get(2)->GetObject<ConstantPositionMobilityModel>();
    pos2->SetPosition(Vector(300, 100, 0));
    Ptr<ConstantPositionMobilityModel> pos3 = nodes.Get(3)->GetObject<ConstantPositionMobilityModel>();
    pos3->SetPosition(Vector(200, 200, 0));
    Ptr<ConstantPositionMobilityModel> pos4 = nodes.Get(4)->GetObject<ConstantPositionMobilityModel>();
    pos4->SetPosition(Vector(400, 200, 0));
    
    std::cout << "=== Node positions set ===" << std::endl;
    
    // 3. Create point-to-point links
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    
    // Store all net devices for IP assignment
    NetDeviceContainer allDevices;
    
    std::cout << "\n=== Creating Network Links ===" << std::endl;
    for (int i = 0; i < NUM_NODES; i++) {
        for (int j = i + 1; j < NUM_NODES; j++) {
            if (adj[i][j] != INF) {
                NodeContainer pair = NodeContainer(nodes.Get(i), nodes.Get(j));
                NetDeviceContainer link = p2p.Install(pair);
                allDevices.Add(link);
                std::cout << "Link: " << i << " <-> " << j 
                          << " (weight: " << adj[i][j] << ")" << std::endl;
            }
        }
    }
    
    // 4. Install Internet stack
    InternetStackHelper internet;
    internet.Install(nodes);
    std::cout << "\n=== Internet stack installed ===" << std::endl;
    
    // 5. Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(allDevices);
    
    std::cout << "=== IP Addresses assigned ===" << std::endl;
    for (uint32_t i = 0; i < interfaces.GetN(); i++) {
        std::cout << "Interface " << i << ": " << interfaces.GetAddress(i) << std::endl;
    }
    
    // 6. Get primary IP for each node
    Ipv4Address nodePrimaryIP[NUM_NODES];
    for (uint32_t i = 0; i < nodes.GetN(); i++) {
        Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
        // Find the first non-loopback interface
        for (uint32_t j = 0; j < ipv4->GetNInterfaces(); j++) {
            if (ipv4->IsUp(j) && j != 0) { // Skip loopback (index 0)
                Ipv4InterfaceAddress addr = ipv4->GetAddress(j, 0);
                nodePrimaryIP[i] = addr.GetLocal();
                break;
            }
        }
        std::cout << "Node " << i << " primary IP: " << nodePrimaryIP[i] << std::endl;
    }
    
    // 7. Install Dijkstra routing tables
    std::cout << "\n=== Installing Dijkstra Routing Tables ===" << std::endl;
    for (uint32_t i = 0; i < nodes.GetN(); i++) {
        Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> staticRouting = ipv4->GetRoutingProtocol()
            ->GetObject<Ipv4StaticRouting>();
        
        if (!staticRouting) {
            std::cout << "Could not get static routing for node " << i << std::endl;
            continue;
        }
        
        auto routingTable = dijkstraWithNextHop(i, NUM_NODES, adj);
        std::cout << "\nNode " << i << " routing entries:" << std::endl;
        
        for (const auto& entry : routingTable) {
            int dest, nextHop, cost;
            std::tie(dest, nextHop, cost) = entry;
            
            if (dest != i && nextHop != -1) {
                // Find which interface to use (the one connected to nextHop)
                // For simplicity, use interface 1 (first non-loopback)
                uint32_t iface = 1;
                
                staticRouting->AddHostRouteTo(
                    nodePrimaryIP[dest],
                    nodePrimaryIP[nextHop],
                    iface
                );
                std::cout << "  → dest " << dest << " (" << nodePrimaryIP[dest]
                          << ") via " << nextHop << " (" << nodePrimaryIP[nextHop]
                          << ") cost: " << cost << std::endl;
            }
        }
    }
    
    // 8. Setup applications - UDP Echo
    uint16_t port = 9;
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApps = echoServer.Install(nodes.Get(4));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(10.0));
    std::cout << "\n=== Server installed on Node 4 (" << nodePrimaryIP[4] << ")" << std::endl;
    
    UdpEchoClientHelper echoClient(nodePrimaryIP[4], port);
    echoClient.SetAttribute("MaxPackets", UintegerValue(5));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));
    
    ApplicationContainer clientApps = echoClient.Install(nodes.Get(0));
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(10.0));
    std::cout << "=== Client installed on Node 0 → sends to Node 4 ===" << std::endl;
    
    // 9. Enable PCAP tracing
    p2p.EnablePcapAll("lora-mesh", true);
    std::cout << "\n=== PCAP tracing enabled ===" << std::endl;
    
    // 10. NetAnim
    AnimationInterface anim("lora-mesh.xml");
    anim.EnablePacketMetadata(true);
    anim.UpdateNodeDescription(nodes.Get(0), "Node 0 (Source)");
    anim.UpdateNodeDescription(nodes.Get(1), "Node 1");
    anim.UpdateNodeDescription(nodes.Get(2), "Node 2");
    anim.UpdateNodeDescription(nodes.Get(3), "Node 3");
    anim.UpdateNodeDescription(nodes.Get(4), "Node 4 (Dest)");
    anim.UpdateNodeColor(nodes.Get(0), 0, 200, 0);
    anim.UpdateNodeColor(nodes.Get(4), 200, 0, 0);
    anim.UpdateNodeColor(nodes.Get(1), 200, 200, 0);
    anim.UpdateNodeColor(nodes.Get(2), 0, 200, 200);
    anim.UpdateNodeColor(nodes.Get(3), 200, 0, 200);
    
    std::cout << "=== NetAnim XML: lora-mesh.xml ===" << std::endl;
    
    // 11. Run simulation
    std::cout << "\n=== Running Simulation (15 seconds) ===" << std::endl;
    Simulator::Stop(Seconds(15.0));
    Simulator::Run();
    Simulator::Destroy();
    
    std::cout << "\n=== Simulation Complete ===" << std::endl;
    std::cout << "Output files: lora-mesh.xml, lora-mesh-*.pcap" << std::endl;
    
    return 0;
}
