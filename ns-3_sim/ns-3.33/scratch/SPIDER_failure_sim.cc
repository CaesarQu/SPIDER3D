/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/****************************************************************************/
/* This file is part of SPIDER project.                                       */
/*                                                                          */
/* SPIDER is free software: you can redistribute it and/or modify             */
/* it under the terms of the GNU General Public License as published by     */
/* the Free Software Foundation, either version 3 of the License, or        */
/* (at your option) any later version.                                      */
/*                                                                          */
/* SPIDER is distributed in the hope that it will be useful,                  */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of           */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            */
/* GNU General Public License for more details.                             */
/*                                                                          */
/* You should have received a copy of the GNU General Public License        */
/* along with SPIDER.  If not, see <http://www.gnu.org/licenses/>.            */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*  Author:    Dmitrii Chemodanov, University of Missouri-Columbia          */
/*  Title:     SPIDER: AI-augmented Geographic Routing Approach for IoT-based */
/*             Incident-Supporting Applications                             */
/*  Revision:  1.0         6/19/2017                                        */
/****************************************************************************/


#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/v4ping-helper.h"
#include "ns3/v4ping.h"
#include "ns3/csma-module.h"
#include "ns3/mesh-helper.h"
#include "ns3/wifi-module.h"
#include "ns3/mesh-module.h"
#include "ns3/olsr-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/mobility-module.h"
#include "ns3/agra-module.h"
#include "ns3/spider-module.h"
#include "ns3/aodv-module.h"
#include "ns3/random-variable-stream.h"
#include "ns3/energy-module.h"
#include "ns3/object-ptr-container.h"
#include "ns3/object-vector.h"
#include <fstream>

//#include "myapp.h"

NS_LOG_COMPONENT_DEFINE ("MyExpr");

using namespace ns3;

// We want to look at changes in the ns-3 Udp congestion window.  We need
// to crank up a flow and hook the CongestionWindow attribute on the socket
// of the sender.  Normally one would use an on-off application to generate a
// flow, but this has a couple of problems.  First, the socket of the on-off
// application is not created until Application Start time, so we wouldn't be
// able to hook the socket (now) at configuration time.  Second, even if we
// could arrange a call after start time, the socket is not public so we
// couldn't get at it.
//
// So, we can cook up a simple version of the on-off application that does what
// we want.  On the plus side we don't need all of the complexity of the on-off
// application.  On the minus side, we don't have a helper, so we have to get
// a little more involved in the details, but this is trivial.
//
// So first, we create a socket and do the trace connect on it; then we pass
// this socket into the constructor of our simple application which we then
// install in the source node.
// ===========================================================================
//
class MyApp : public Application
{
public:

  MyApp ();
  virtual ~MyApp();

  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, DataRate dataRate);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void ScheduleTx (void);
  void SendPacket (void);

  Ptr<Socket>     m_socket;
  Address         m_peer;
  uint32_t        m_packetSize;
  DataRate        m_dataRate;
  EventId         m_sendEvent;
  bool            m_running;
  uint32_t        m_packetsSent;
};

MyApp::MyApp ()
  : m_socket (0),
    m_peer (),
    m_packetSize (0),
    m_dataRate (0),
    m_sendEvent (),
    m_running (false),
    m_packetsSent (0)
{
}

MyApp::~MyApp()
{
  m_socket = 0;
}

void
MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, DataRate dataRate)
{
  m_socket = socket;
  m_peer = address;
  m_packetSize = packetSize;
  m_dataRate = dataRate;
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  SendPacket ();
}

void
MyApp::StopApplication (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }

  if (m_socket)
    {
      m_socket->Close ();
    }
}

void
MyApp::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_socket->Send (packet);
//  std::cout << Simulator::Now ().GetSeconds () << "\t" << "one packet has been sent! PacketSize="<<m_packetSize<<"\n";
  ScheduleTx ();
}

void
MyApp::ScheduleTx (void)
{
  if (m_running)
    {
      Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ())));
      m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);
    }
}

static void
SetNodePos(Ptr<Node> node, Vector pos)
{
	Ptr<MobilityModel> mobility = node->GetObject<MobilityModel> ();
	mobility->SetPosition(pos);
}

static void
SetNodeFailure (Ptr<Node> node, Time FailureTime)
{
  Ptr<MobilityModel> mobility = node->GetObject<MobilityModel> ();
  Vector oldPos = mobility->GetPosition();
  Vector failurePos (10000,0,0); //to simulate failure move node out of its neighbors range

  Simulator::Schedule (Simulator::Now(), &SetNodePos, node, failurePos);
  Simulator::Schedule (Simulator::Now() + FailureTime - Seconds(0.1), &SetNodePos,node, oldPos);
}

static void
DecideOnNodesFailure (NodeContainer nodes, Time FailureTime, double FailureProb, Time StopTime)
{
	Ptr<UniformRandomVariable> rvar = CreateObject<UniformRandomVariable>();
	NodeContainer::Iterator i;
    for (i = nodes.Begin(); !(i == nodes.End()); i++) {
		if (rvar->GetValue (0, 1) < FailureProb)
		{
			SetNodeFailure(*i, FailureTime);
			std::cout<< Simulator::Now ().GetSeconds () <<" sec, Node["<<(*i)->GetId()<<"] failed!"<<std::endl;
		}
	}

   	Simulator::Schedule (FailureTime, &DecideOnNodesFailure, nodes, FailureTime, FailureProb, StopTime);
}

static void  
EnergyRemaning(Ptr<OutputStreamWrapper> stream, EnergySourceContainer sources)
{
        double sum_energy=0;
        for(uint16_t n=0;n<sources.GetN();n++)
        {
                //Ptr<Node> l = DynamicCast<Node> (sources.Get (n));
                //std::cout<<"Node Ipvaddress = "<<l->GetId()<<std::endl;
                Ptr<BasicEnergySource> basicSourcePtr = DynamicCast<BasicEnergySource> (sources.Get (n));
		Ptr<DeviceEnergyModel> basicRadioModelPtr = basicSourcePtr->FindDeviceEnergyModels("ns3::WifiRadioEnergyModel").Get(0);	
                //std::cout<<"Energy consumped at node ["<< n<<"] = "<<1000 - basicSourcePtr->GetRemainingEnergy()<<std::endl;
                sum_energy += basicSourcePtr->GetRemainingEnergy();//(1000 - basicRadioModelPtr->GetTotalEnergyConsumption());
                *stream->GetStream () << basicSourcePtr->GetRemainingEnergy() << std::endl;
        }
        std::cout<<"Energy consumped at time ["<<Simulator::Now().GetSeconds ()<<"] overall = "<<sum_energy<<std::endl;
        *stream->GetStream () << sum_energy << std::endl;
        //Simulator::Schedule (Seconds(60), &EnergyRemaning, stream, sources);
}

static void
CourseChange (std::string foo, Ptr<const MobilityModel> mobility)
{
  Vector pos = mobility->GetPosition ();
  std::cout << Simulator::Now ().GetSeconds () << ", Paramedic has changed location -> POS: (" << pos.x << ", " << pos.y << ")." << std::endl;
}

static void
CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
  //NS_LOG_UNCOND (Simulator::Now ().GetSeconds () << "\t" << newCwnd);
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}
/*
static void
RxDrop (Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p)
{
  //NS_LOG_UNCOND ("RxDrop at " << Simulator::Now ().GetSeconds ());
  *stream->GetStream () << "Rx drop at: "<< Simulator::Now ().GetSeconds () << std::endl;
}

static void
PingRtt1 (ApplicationContainer apps)
{
        //double rtt = (double)apps.Get(0)->GetObject<V4Ping>()->m_avgRtt.Max();
        //std::cout<<" "<<rtt<<std::endl;
        //apps.Get(0)->GetObject<V4Ping>()->StartApplication();
}
*/
static void 
SaveTh(Ptr<OutputStreamWrapper> stream, Ptr<PacketSink> sink, double oldCur)
{
  double time = Simulator::Now().GetSeconds();
  double cur = (sink->GetTotalRx() - oldCur) * (double) 8/ 1000000;
  std::cout<<"Throughput at time ["<<time<<"] overall = "<<cur/5<<std::endl;
  *stream->GetStream () << cur/5 << std::endl;
  Simulator::Schedule(Seconds(5), &SaveTh, stream, sink, sink->GetTotalRx());
}

int main (int argc, char *argv[])
{
  bool enableFlowMonitor = false;
  //ErpOfdmRate54Mbps
  //DsssRate11Mbps
  std::string phyMode ("ErpOfdmRate54Mbps");
  uint16_t RepulsionMode = (uint16_t) 0;
  //Init the location and radius of obstacle
  //double locationX=200,locationY=325;
  //double object_radius=350;
  double lambda=0;
  Time FailureTime = Seconds(30.0);
  double FailureProb = 0.05;
  Time StopTime = Seconds(720);//780.0);
  Time LocationTime = Seconds(180.0);
  double SrcSpeed = 2.8; //[m/s] speed of jogging
  std::cout<<"lambda value = "<<lambda<<std::endl;
  

  CommandLine cmd;
  cmd.AddValue ("EnableMonitor", "Enable Flow Monitor", enableFlowMonitor);
  cmd.AddValue ("phyMode", "Wifi Phy mode", phyMode);
  cmd.AddValue ("RepulsionMode", "Enable Repulsion during Greedy Forwarding", RepulsionMode);
  cmd.AddValue ("FailureTime", "Nodes Failure Time", FailureTime);
  cmd.AddValue ("FailureProb", "Nodes Failure Probability", FailureProb);
  cmd.AddValue ("StopTime", "Time to Stop Simulation", StopTime);
  cmd.AddValue ("LocationTime", "Time src spends at each location", LocationTime);
  cmd.AddValue ("SrcSpeed", "Speed of the paramedic who acts as a src between locations", SrcSpeed);
  cmd.Parse (argc, argv);

//
// Explicitly create the nodes required by the topology (shown above).
//
  NS_LOG_INFO ("Create nodes.");
  NodeContainer c1; // sink and source
  c1.Create(14);
  NodeContainer c2;
  c2.Create(14);
  NodeContainer c3;
  c3.Create(5);
  NodeContainer c4;
  c4.Create(5);

  NodeContainer allTransmissionNodes;
  allTransmissionNodes.Add(c1);
  allTransmissionNodes.Add(c2);
  allTransmissionNodes.Add(c3);
  allTransmissionNodes.Add(c4);

  NodeContainer sinkSrc;
  sinkSrc.Create(2);

  //add to one container
  NodeContainer c;
  c.Add(sinkSrc); //add sink and source
  c.Add(allTransmissionNodes);


  // Set up WiFi
  WifiHelper wifi;

  YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper();
  wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11);

  YansWifiChannelHelper wifiChannel ;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss ("ns3::TwoRayGroundPropagationLossModel",
	  	  	  	  	  	  	  	    "SystemLoss", DoubleValue(1),
		  	  	  	  	  	  	    "HeightAboveZ", DoubleValue(1.5));

  // For range near 250m
  wifiPhy.Set ("TxPowerStart", DoubleValue(20));
  wifiPhy.Set ("TxPowerEnd", DoubleValue(20));
  wifiPhy.Set ("TxPowerLevels", UintegerValue(1));
  wifiPhy.Set ("TxGain", DoubleValue(6));
  wifiPhy.Set ("RxGain", DoubleValue(0));
  //wifiPhy.Set ("SignalDetectionThreshold", DoubleValue(-64.5));//-64.5));
  //wifiPhy.Set ("EdThreshold", DoubleValue(-71.8));//-67.5));

  wifiPhy.SetChannel (wifiChannel.Create ());
  NetDeviceContainer devices;
  //for all cases except HWMP uncomment below ============================================
  // Add a non-QoS upper mac
/*
  NqosWifiMacHelper wifiMac = NqosWifiMacHelper::Default ();
  wifiMac.SetType ("ns3::AdhocWifiMac");

  // Set 802.11g standard
  wifi.SetStandard (WIFI_PHY_STANDARD_80211g);

  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode",StringValue(phyMode),
                                "ControlMode",StringValue(phyMode));
  devices = wifi.Install (wifiPhy, wifiMac, c);
*/
  //============================================
  
    //setup routing protocol
  //for HWMP uncomment below ============================================
  
         std::string root = "ff:ff:ff:ff:ff:ff";
         MeshHelper mesh = MeshHelper::Default(); 
         mesh.SetRemoteStationManager ("ns3::ConstantRateWifiManager");
         mesh.SetStackInstaller ("ns3::Dot11sStack"); 
         mesh.SetSpreadInterfaceChannels (MeshHelper::SPREAD_CHANNELS); 
         mesh.SetNumberOfInterfaces (1);
         devices = mesh.Install (wifiPhy, c);
  
  //============================================

  //==============Set up battery lion=========
  BasicEnergySourceHelper basicSourceHelper;
  // configure energy source
  basicSourceHelper.Set ("BasicEnergySourceInitialEnergyJ", DoubleValue (1000));
  // install source
  EnergySourceContainer sources = basicSourceHelper.Install (c);
  
  // configure mesh energy model
  // uncomment for hwmp
//  WifiRadioEnergyModelHelper wifiEnergyHelper; 
//  wifiEnergyHelper.Set ("TxCurrentA", DoubleValue (0.0174));
//  wifiEnergyHelper.Set ("RxCurrentA", DoubleValue (0.0174));
//  DeviceEnergyModelContainer deviceModels = wifiEnergyHelper.DoInstall (devices, sources); 

  // configure radio energy model 
  // uncomment for other routing
  WifiRadioEnergyModelHelper radioEnergyHelper; 
  radioEnergyHelper.Set ("TxCurrentA", DoubleValue (0.0174)); 
  radioEnergyHelper.Set ("RxCurrentA", DoubleValue (0.0174));
  DeviceEnergyModelContainer deviceModels = radioEnergyHelper.Install (devices, sources); 
  //==========================================

/*
  AgraHelper agra;
  agra.Set("RepulsionMode",UintegerValue(RepulsionMode));
  agra.Set("locationX",DoubleValue(locationX));
  agra.Set("locationY",DoubleValue(locationY));
  agra.Set("object_radius",DoubleValue(object_radius));

  SpiderHelper spider;
  spider.Set("RepulsionMode",UintegerValue(RepulsionMode));
  spider.Set("locationX",DoubleValue(locationX));
  spider.Set("locationY",DoubleValue(locationY));
  spider.Set("object_radius",DoubleValue(object_radius));
  spider.Set("lambda",DoubleValue(lambda));
*/
  // Enable OLSR
  OlsrHelper olsr;
  Ipv4StaticRoutingHelper staticRouting;

  Ipv4ListRoutingHelper list;
  list.Add (staticRouting, 0);
  list.Add (olsr, 10);

  AodvHelper aodv;
/*
   2. Setup Udp/IP & AODV
    AodvHelper aodv; // Use default parameters here
    InternetStackHelper internetStack;
    internetStack.SetRoutingHelper (aodv);
    internetStack.Install (*m_nodes);
    streamsUsed += internetStack.AssignStreams (*m_nodes, streamsUsed);
    // InternetStack uses m_size more streams
    NS_TEST_ASSERT_MSG_EQ (streamsUsed, (devices.GetN () * 8) + m_size, "Stream assignment mismatch");
    streamsUsed += aodv.AssignStreams (*m_nodes, streamsUsed);
    // AODV uses m_size more streams
    NS_TEST_ASSERT_MSG_EQ (streamsUsed, ((devices.GetN () * 8) + (2*m_size)), "Stream assignment mismatch");
*/

  InternetStackHelper internet;
  //internet.SetRoutingHelper (agra);
  //internet.SetRoutingHelper (spider);
  //internet.SetRoutingHelper (list);
  //internet.SetRoutingHelper (aodv); // has effect on the next Install ()
  internet.Install (c);

  //istall spider headers to all nodes
//  spider.Install ();
//  agra.Install();

  // Set up Addresses
  Ipv4AddressHelper ipv4;
  NS_LOG_INFO ("Assign IP Addresses.");
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ifcont = ipv4.Assign (devices);

  // Create pinger
/*
  V4PingHelper ping = V4PingHelper(ifcont.GetAddress(1));
  ping.SetAttribute ("Verbose", BooleanValue (true));
  NodeContainer n;
  n.Add(sinkSrc.Get(0));
  ApplicationContainer apps = ping.Install(n);
  apps.Start (StopTime - Seconds (5));
  apps.Stop (StopTime - Seconds (0.001));
*/
// Set Mobility for all nodes
  MobilityHelper mobility1;
  mobility1.SetPositionAllocator ("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue (50.0),
                                 "MinY", DoubleValue (0.0),
                                 "DeltaX", DoubleValue (50),
                                 "DeltaY", DoubleValue (50),
                                 "GridWidth", UintegerValue (1),
                                 "LayoutType", StringValue ("RowFirst"));
  mobility1.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility1.Install(c1);

  MobilityHelper mobility2;
  mobility2.SetPositionAllocator ("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue (350.0),
                                 "MinY", DoubleValue (0.0),
                                 "DeltaX", DoubleValue (50),
                                 "DeltaY", DoubleValue (50),
                                 "GridWidth", UintegerValue (1),
                                 "LayoutType", StringValue ("RowFirst"));
  mobility2.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility2.Install(c2);

  MobilityHelper mobility3;
  mobility3.SetPositionAllocator ("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue (100.0),
                                 "MinY", DoubleValue (0.0),
                                 "DeltaX", DoubleValue (50),
                                 "DeltaY", DoubleValue (50),
                                 "GridWidth", UintegerValue (5),
                                 "LayoutType", StringValue ("RowFirst"));
  mobility3.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility3.Install(c3);

  MobilityHelper mobility4;
  mobility4.SetPositionAllocator ("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue (100.0),
                                 "MinY", DoubleValue (650.0),
                                 "DeltaX", DoubleValue (50),
                                 "DeltaY", DoubleValue (50),
                                 "GridWidth", UintegerValue (5),
                                 "LayoutType", StringValue ("RowFirst"));
  mobility4.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility4.Install(c4);

  //sink is static and represents adhoc metwork edge, e.g., internet gateway
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject <ListPositionAllocator>();
//  positionAlloc ->Add(Vector(0, 325, 0)); // source
  positionAlloc ->Add(Vector(400, 325, 0)); // sink
  mobility.SetPositionAllocator(positionAlloc);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(sinkSrc.Get(1));

  //paramedic acts as a src and moves from location 1 to location 3 through location 2.

   MobilityHelper mobilitySrc;
  Ptr<ListPositionAllocator> positionAllocSrc = CreateObject <ListPositionAllocator>();
  positionAllocSrc ->Add(Vector(0, 0, 0)); // source
  mobilitySrc.SetPositionAllocator(positionAllocSrc);
  mobilitySrc.SetMobilityModel ("ns3::WaypointMobilityModel",
                               "InitialPositionIsWaypoint", BooleanValue (true));
  mobilitySrc.Install(sinkSrc.Get(0));

  Waypoint location1 (LocationTime, Vector(0,0,0));
  Time nextWaypointTime = LocationTime + Seconds(325/SrcSpeed);
  Waypoint location21 (nextWaypointTime, Vector(0,325,0));
  Waypoint location22 (nextWaypointTime + LocationTime, Vector(0,325,0));
  Time lastWaypointTime = nextWaypointTime + LocationTime + Seconds(325/SrcSpeed);
  Waypoint location3 (lastWaypointTime, Vector(0,650,0));

  //Add waypoints to the src
  Ptr<WaypointMobilityModel> srcModel = sinkSrc.Get(0)->GetObject<WaypointMobilityModel> ();
  srcModel->AddWaypoint(location1);
  srcModel->AddWaypoint(location21);
  srcModel->AddWaypoint(location22);
  srcModel->AddWaypoint(location3);

  //setup applications
  NS_LOG_INFO ("Create Applications.");

  uint16_t sinkPort = 8080;
  Address sinkAddress (InetSocketAddress (ifcont.GetAddress (1), sinkPort));
  PacketSinkHelper packetSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
  ApplicationContainer sinkApps = packetSinkHelper.Install (sinkSrc.Get (1));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (StopTime);

  Ptr<Socket> ns3UdpSocket = Socket::CreateSocket (sinkSrc.Get (0), UdpSocketFactory::GetTypeId ());
  //ns3UdpSocket->TraceConnectWithoutContext ("CongestionWindow", MakeCallback (&CwndChange));

  Ptr<MyApp> app = CreateObject<MyApp> ();
  app->Setup (ns3UdpSocket, sinkAddress, 1448, DataRate ("5Mbps"));
  sinkSrc.Get (0)->AddApplication (app);
  app->SetStartTime (Seconds (1.0));
  app->SetStopTime (StopTime);

  AsciiTraceHelper asciiTraceHelper;
  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream ("cwnd_mobicom_expr.txt");
  ns3UdpSocket->TraceConnectWithoutContext ("CongestionWindow", MakeBoundCallback (&CwndChange, stream));


  //Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream ("pdrop_mobicom_expr.txt");
  //devices.Get (1)->TraceConnectWithoutContext ("PhyRxDrop", MakeBoundCallback (&RxDrop, stream2));
  //devices.Get (1)->TraceConnectWithoutContext ("PhyRxDrop", MakeCallback (&RxDrop));

  /*
  V4PingHelper ping (ifcont.GetAddress (1));
  ping.SetAttribute ("Verbose", BooleanValue (true));
  ping.SetAttribute ("Interval", TimeValue (Seconds(60))); 
  ApplicationContainer p = ping.Install (sinkSrc.Get (0));
  p.Start (Seconds (5));
  p.Stop (StopTime - Seconds (0.001));
  */
  std::cout <<"src ip="<<ifcont.GetAddress (0, 0)<<" id="<<sinkSrc.Get (0)->GetId() <<"; sink ip="<<ifcont.GetAddress(1, 0)<<" id="<<sinkSrc.Get (1)->GetId() <<std::endl;


  //log only src movements
  Config::Connect ("/NodeList/38/$ns3::MobilityModel/CourseChange",
                     MakeCallback (&CourseChange));

// Trace devices (pcap)
//  wifiPhy.EnablePcap ("mobicom_expr", devices.Get(0)); //save pcap file for src
wifiPhy.EnablePcap ("mobicom_expr", devices.Get(1)); //save pcap file for sink

  //Config::Connect("/NodeList/*/ApplicationList/*/$ns3::V4Ping/Rtt", MakeCallback(&PingRtt));
  //Simulator::Schedule (FailureTime, &EnergyRemaning, sources);
  Ptr<PacketSink> sink = StaticCast<PacketSink> (sinkApps.Get(0));
  double oldCur = sink -> GetTotalRx();
  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream ("thr_low_failure_HWMP.txt");
  Simulator::Schedule(Seconds(0), &SaveTh, stream1, sink, oldCur);
  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream ("energy_low_failure_HWMP.txt");
  Simulator::Schedule(StopTime, &EnergyRemaning, stream2, sources);

// Now, do the actual simulation.
  NS_LOG_INFO ("Run Simulation.");

  DecideOnNodesFailure (allTransmissionNodes, FailureTime, FailureProb, StopTime); // simulate node failures by moving them far from others

  Simulator::Stop (StopTime);
  Simulator::Run ();
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");
}
