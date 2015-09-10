#include "tor-dumbbell-helper.h"

TorDumbbellHelper::TorDumbbellHelper ()
{
  // Based on the iPlane data set: latencies 2015-08-04
  //RTT between ["NA", "NA"] (min/Q1/Median/Q3/max in ms) 11 29 45.0 73 148
  Ptr<EmpiricalRandomVariable> owd = CreateObject<EmpiricalRandomVariable> ();
  owd->CDF ( 11/2.0,0.00);
  owd->CDF ( 29/2.0,0.25);
  owd->CDF ( 45/2.0,0.50);
  owd->CDF ( 73/2.0,0.75);
  owd->CDF (148/2.0,1.00);
  uint32_t leftDelay = owd->GetInteger ();

  // RTT between ["EU", "EU"] (min/Q1/Median/Q3/max in ms) 14 27 37.0 48 65
  owd = CreateObject<EmpiricalRandomVariable> ();
  owd->CDF ( 14/2.0,0.00);
  owd->CDF ( 27/2.0,0.25);
  owd->CDF ( 37/2.0,0.50);
  owd->CDF ( 48/2.0,0.75);
  owd->CDF ( 65/2.0,1.00);
  uint32_t rightDelay = owd->GetInteger ();

  // RTT between ["EU", "NA"] (min/Q1/Median/Q3/max in ms) 78 117 132.0 161 252
  owd = CreateObject<EmpiricalRandomVariable> ();
  owd->CDF ( 78/2.0,0.00);
  owd->CDF (117/2.0,0.25);
  owd->CDF (132/2.0,0.50);
  owd->CDF (161/2.0,0.75);
  owd->CDF (252/2.0,1.00);
  uint32_t routerDelay = owd->GetInteger ();
  routerDelay = max (routerDelay-(rightDelay+leftDelay), (uint32_t) 1);

  m_p2pLeftHelper.SetChannelAttribute ("Delay", TimeValue (MilliSeconds(leftDelay)));
  m_p2pRightHelper.SetChannelAttribute ("Delay", TimeValue (MilliSeconds(rightDelay)));
  m_p2pRouterHelper.SetChannelAttribute ("Delay", TimeValue (MilliSeconds(routerDelay)));

  m_p2pLeftHelper.SetDeviceAttribute ("DataRate", StringValue ("10Gb/s"));
  m_p2pRightHelper.SetDeviceAttribute ("DataRate", StringValue ("10Gb/s"));
  m_p2pRouterHelper.SetDeviceAttribute ("DataRate", StringValue ("10Gb/s"));

  m_dumbbellHelper = 0;
  m_nLeftLeaf = 0;
  m_nRightLeaf = 0;

  m_disableProxies = false;

  m_bulkRequest = CreateObject<ConstantRandomVariable> ();
  m_bulkRequest->SetAttribute ("Constant", DoubleValue (5 * 1024 * 1024));
  m_bulkThink = CreateObject<ConstantRandomVariable> ();
  m_bulkThink->SetAttribute ("Constant", DoubleValue (0));

  m_clientRequest = CreateObject<ConstantRandomVariable> ();
  m_clientRequest->SetAttribute ("Constant", DoubleValue (320 * 1024));
  m_clientThink = CreateObject<UniformRandomVariable> ();
  m_clientThink->SetAttribute ("Min", DoubleValue (1.0));
  m_clientThink->SetAttribute ("Max", DoubleValue (20.0));

  m_rng = CreateObject<UniformRandomVariable> ();
  m_startTimeStream = CreateObject<UniformRandomVariable> ();
  m_startTimeStream->SetAttribute ("Min", DoubleValue (0.01));
  m_startTimeStream->SetAttribute ("Max", DoubleValue (1.0));

  m_factory.SetTypeId ("ns3::TorApp");
}

TorDumbbellHelper::~TorDumbbellHelper ()
{
  if (m_dumbbellHelper)
    {
      delete m_dumbbellHelper;
    }
}

void
TorDumbbellHelper::AddCircuit (int id, string entryName, string middleName, string exitName, string typehint)
{
  NS_ASSERT (m_circuits.find (id) == m_circuits.end ());
  CircuitDescriptor desc;
  if (typehint == "bulk")
    {
      desc = CircuitDescriptor (id, GetProxyName (id), entryName, middleName, exitName, typehint,
              CreateObject<PseudoClientSocket> (m_bulkRequest, m_bulkThink, Seconds(m_startTimeStream->GetValue ())) );
    }
  else if (typehint == "web")
    {
      desc = CircuitDescriptor (id, GetProxyName (id), entryName, middleName, exitName, typehint,
              CreateObject<PseudoClientSocket> (m_clientRequest, m_clientThink, Seconds(m_startTimeStream->GetValue ())) );
    }
  m_circuits[id] = desc;
  circuitIds.push_back (id);
}

void
TorDumbbellHelper::AddRelay (string name, string continent)
{
  if (m_relays.find (name) == m_relays.end ())
    {

      if (continent.size () == 0)
        {
          continent = m_rng->GetValue () < 0.5 ? "NA" : "EU";
        }

      RelayDescriptor desc;
      if (continent == "NA")
        {
          desc = RelayDescriptor (name, continent, m_nLeftLeaf++, CreateTorApp ());
        }
      else
        {
          desc = RelayDescriptor (name, continent, m_nRightLeaf++, CreateTorApp ());
        }

      m_relays[name] = desc;
      m_relayApps.Add (desc.tapp);
    }
}

void
TorDumbbellHelper::SetRelayAttribute (string relayName, string attrName, const AttributeValue &value)
{
  NS_ASSERT (m_relays.find (relayName) != m_relays.end ());
  GetTorApp (relayName)->SetAttribute (attrName, value);
}

void
TorDumbbellHelper::EnableNscStack (bool enableNscStack, string nscTcpCong)
{
  if (enableNscStack)
    {
      m_nscTcpCong = nscTcpCong;
    }
  else
    {
      m_nscTcpCong = "";
    }
}

void
TorDumbbellHelper::SetTorAppType (string type)
{
  m_factory.SetTypeId (type);
}

void
TorDumbbellHelper::SetStartTimeStream (Ptr<RandomVariableStream> startTimeStream)
{
  m_startTimeStream = startTimeStream;
}

void
TorDumbbellHelper::DisableProxies (bool disableProxies)
{
  m_disableProxies = disableProxies;
}

void
TorDumbbellHelper::RegisterTtfbCallback (void (*ttfb)(int, double, string))
{
  NS_ASSERT (m_circuits.size () > 1 );
  map<int,CircuitDescriptor>::iterator i;
  for (i = m_circuits.begin (); i != m_circuits.end (); ++i)
    {
      CircuitDescriptor desc = i->second;
      desc.m_clientSocket->SetTtfbCallback (ttfb, desc.id, desc.m_typehint);
    }
}

void
TorDumbbellHelper::RegisterTtlbCallback (void (*ttlb)(int, double, string))
{
  NS_ASSERT (m_circuits.size () > 0);
  map<int,CircuitDescriptor>::iterator i;
  for (i = m_circuits.begin (); i != m_circuits.end (); ++i)
    {
      CircuitDescriptor desc = i->second;
      desc.m_clientSocket->SetTtlbCallback (ttlb, desc.id, desc.m_typehint);
    }
}


void
TorDumbbellHelper::ParseFile (string filename, uint32_t m, double bulkFraction)
{
  ifstream f (filename.c_str ());
  NS_ASSERT (f.is_open ());

  set<uint32_t> chosenCircuits;
  if (m > 0)
    {
      string line;
      uint32_t n;
      for (n = 0; getline (f, line); ++n)
        {
        }
      NS_ASSERT (m <= n);
      while (chosenCircuits.size () < m)
        {
          chosenCircuits.insert (m_rng->GetInteger (1,n));
        }
      f.close ();
      f.open (filename.c_str ());
    }

  uint32_t nBulkclients = ceil (bulkFraction * m);

  int id;
  string path[3], continent[3], bw[3], dummy;
  uint32_t lineno = 0;
  while (f >> id >> path[0] >> continent[0] >> bw[0] >> path[1] >> continent[1] >> bw[1] >> path[2] >> continent[2] >> bw[2])
    {
      ++lineno;
      if (m > 0 && chosenCircuits.find (lineno) == chosenCircuits.end ())
        {
          continue;
        }

      if (nBulkclients > 0)
        {
          AddCircuit (id, path[0], path[1], path[2], "bulk");
          --nBulkclients;
        }
      else
        {
          AddCircuit (id, path[0], path[1], path[2], "web");
        }

      if (!m_disableProxies)
        {
          AddRelay (GetProxyName (id));
        }

      for (int i = 0; i < 3; ++i)
        {
          AddRelay (path[i],continent[i]);
          SetRelayAttribute (path[i], "BandwidthRate", DataRateValue (DataRate (bw[i] + "B/s")));
          SetRelayAttribute (path[i], "BandwidthBurst", DataRateValue (DataRate (bw[i] + "B/s")));
        }
    }
  f.close ();
}


void
TorDumbbellHelper::BuildTopology ()
{
  m_dumbbellHelper = new PointToPointDumbbellHelper (m_nLeftLeaf, m_p2pLeftHelper, m_nRightLeaf, m_p2pRightHelper, m_p2pRouterHelper);

  //install stack
  m_stackHelper.Install (m_dumbbellHelper->GetLeft ());
  m_stackHelper.Install (m_dumbbellHelper->GetRight ());

  //use the linux protocol stack for the spokes
  if (m_nscTcpCong.size () > 0)
    {
      string nscStack = "liblinux2.6.26.so";
      m_stackHelper.SetTcp ("ns3::NscTcpL4Protocol","Library",StringValue (nscStack));
      if (m_nscTcpCong != "cubic")
        {
          Config::Set ("/NodeList/*/$ns3::Ns3NscStack<linux2.6.26>/net.ipv4.tcp_congestion_control", StringValue (m_nscTcpCong));
        }
    }

  for (int i = 0; i < m_nLeftLeaf; ++i)
    {
      m_stackHelper.Install (m_dumbbellHelper->GetLeft (i));
    }

  for (int i = 0; i < m_nRightLeaf; ++i)
    {
      m_stackHelper.Install (m_dumbbellHelper->GetRight (i));
    }

  //assign ipv4
  m_routerIp.SetBase ("10.1.0.0", "255.255.255.253");
  m_leftIp.SetBase ("10.2.0.0", "255.255.255.0");
  m_rightIp.SetBase ("10.128.0.0", "255.255.255.0");
  m_dumbbellHelper->AssignIpv4Addresses (m_leftIp, m_rightIp, m_routerIp);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  InstallCircuits ();
}


void
TorDumbbellHelper::InstallCircuits ()
{
  Ptr<TorBaseApp> clientApp;
  Ptr<TorBaseApp> entryApp;
  Ptr<TorBaseApp> middleApp;
  Ptr<TorBaseApp> exitApp;

  Ipv4AddressHelper ipHelper = Ipv4AddressHelper ("127.0.0.0", "255.0.0.0");

  map<int,CircuitDescriptor>::iterator i;
  for (i = m_circuits.begin (); i != m_circuits.end (); ++i)
    {
      CircuitDescriptor desc = i->second;

      if (!m_disableProxies)
        {
          clientApp = InstallTorApp (desc.proxy ());
        }
      entryApp = InstallTorApp (desc.entry ());
      middleApp = InstallTorApp (desc.middle ());
      exitApp = InstallTorApp (desc.exit ());

      Ipv4Address clientAddress;
      if (!m_disableProxies)
        {
          clientAddress = GetIp (desc.proxy ());
        }

      Ipv4Address entryAddress  = GetIp (desc.entry ());
      Ipv4Address middleAddress = GetIp (desc.middle ());
      Ipv4Address exitAddress   = GetIp (desc.exit ());
      Ipv4Address pseudoServerAddress = ipHelper.NewAddress ();

      exitApp->AddCircuit (desc.id, pseudoServerAddress, SERVEREDGE, middleAddress, RELAYEDGE);
      middleApp->AddCircuit (desc.id, exitAddress, RELAYEDGE, entryAddress, RELAYEDGE);
      if (!m_disableProxies)
        {
          entryApp->AddCircuit(desc.id, middleAddress, RELAYEDGE, clientAddress, RELAYEDGE);
          clientApp->AddCircuit (desc.id, entryAddress, RELAYEDGE, ipHelper.NewAddress (), PROXYEDGE, desc.m_clientSocket);
        }
      else
        {
          entryApp->AddCircuit (desc.id, middleAddress, RELAYEDGE, ipHelper.NewAddress (), PROXYEDGE, desc.m_clientSocket);
        }
    }
}


Ptr<TorBaseApp>
TorDumbbellHelper::InstallTorApp (string name)
{
  NS_ASSERT (m_relays.find (name) != m_relays.end ());
  RelayDescriptor desc = m_relays[name];
  if (GetNode (name)->GetNApplications () == 0 )
    {
      GetNode (name)->AddApplication (desc.tapp);
    }
  return desc.tapp;
}


Ptr<Node>
TorDumbbellHelper::GetNode (string continent, uint32_t id)
{
  if (continent == "NA")
    {
      return m_dumbbellHelper->GetLeft (id);
    }
  else
    {
      return m_dumbbellHelper->GetRight (id);
    }
}

Ptr<Node>
TorDumbbellHelper::GetNode (string name)
{
  RelayDescriptor desc = m_relays[name];
  return GetNode (desc.continent,desc.spokeId);
}

Ipv4Address
TorDumbbellHelper::GetIp (string continent, uint32_t id)
{
  if (continent == "NA")
    {
      return m_dumbbellHelper->GetLeftIpv4Address (id);
    }
  else
    {
      return m_dumbbellHelper->GetRightIpv4Address (id);
    }
}

Ipv4Address
TorDumbbellHelper::GetIp (string name)
{
  RelayDescriptor desc = m_relays[name];
  return GetIp (desc.continent,desc.spokeId);
}

ApplicationContainer
TorDumbbellHelper::GetTorAppsContainer ()
{
  return m_relayApps;
}

Ptr<TorBaseApp>
TorDumbbellHelper::GetTorApp (string name)
{
  return m_relays[name].tapp;
}

Ptr<TorBaseApp>
TorDumbbellHelper::GetExitApp (int id)
{
  if (m_circuits.find (id) == m_circuits.end ())
    {
      return 0;
    }
  CircuitDescriptor desc = m_circuits[id];
  return GetTorApp (desc.exit ());
}

Ptr<TorBaseApp>
TorDumbbellHelper::GetMiddleApp (int id)
{
  if (m_circuits.find (id) == m_circuits.end ())
    {
      return 0;
    }
  CircuitDescriptor desc = m_circuits[id];
  return GetTorApp (desc.middle ());
}

Ptr<TorBaseApp>
TorDumbbellHelper::GetEntryApp (int id)
{
  if (m_circuits.find (id) == m_circuits.end ())
    {
      return 0;
    }
  CircuitDescriptor desc = m_circuits[id];
  return GetTorApp (desc.entry ());
}

Ptr<TorBaseApp>
TorDumbbellHelper::GetProxyApp (int id)
{
  if (m_circuits.find (id) == m_circuits.end ())
    {
      return 0;
    }
  CircuitDescriptor desc = m_circuits[id];
  if (m_disableProxies)
    {
      return GetTorApp (desc.entry ());
    }
  else
    {
      return GetTorApp (desc.proxy ());
    }
}


Ptr<TorBaseApp>
TorDumbbellHelper::CreateTorApp ()
{
  Ptr<TorBaseApp> tapp = m_factory.Create<TorBaseApp> ();
  NS_ASSERT (tapp);
  return tapp;
}


string
TorDumbbellHelper::GetCircuitTypehint (int id)
{
  CircuitDescriptor desc = m_circuits[id];
  return desc.m_typehint;
}

string
TorDumbbellHelper::GetProxyName (int id)
{
  stringstream ss;
  ss << "proxy" << id;
  return ss.str ();
}

void
TorDumbbellHelper::PrintCircuits ()
{
  map<int,CircuitDescriptor>::iterator i;
  for (i = m_circuits.begin (); i != m_circuits.end (); ++i)
    {
      CircuitDescriptor e = i->second;
      cout << e.id << " (" << e.m_typehint << "):";
      if (!m_disableProxies)
        {
          cout << "\t" << e.proxy () << "[" << m_relays[e.proxy ()].continent << "]";
        }
      cout << "\t" << e.entry () << "[" << m_relays[e.entry ()].continent << "]";
      cout << "\t" << e.middle () << "[" << m_relays[e.middle ()].continent << "]";
      cout << "\t" << e.exit () << "[" << m_relays[e.exit ()].continent << "]";
      cout << endl;
    }
}
