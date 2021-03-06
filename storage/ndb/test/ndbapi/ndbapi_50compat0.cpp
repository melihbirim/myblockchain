/*
   Copyright (C) 2007 MyBlockchain AB, 2010 Sun Microsystems, Inc.
    All rights reserved. Use is subject to license terms.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

/* 
 *  ndbapi_simple.cpp: Using synchronous transactions in NDB API
 *
 *  Correct output from this program is:
 *
 *  ATTR1 ATTR2
 *    0    10
 *    1     1
 *    2    12
 *  Detected that deleted tuple doesn't exist!
 *    4    14
 *    5     5
 *    6    16
 *    7     7
 *    8    18
 *    9     9
 *
 */

#include <myblockchain.h>
#include <NdbApi.hpp>
// Used for cout
#include <stdio.h>
#include <NdbOut.hpp>

static void run_application(MYBLOCKCHAIN &, Ndb_cluster_connection &);

#define PRINT_ERROR(code,msg) \
  ndbout << "Error in " << __FILE__ << ", line: " << __LINE__ \
            << ", code: " << code \
            << ", msg: " << msg << "." << endl
#define MYBLOCKCHAINERROR(myblockchain) { \
  PRINT_ERROR(myblockchain_errno(&myblockchain),myblockchain_error(&myblockchain)); \
  exit(-1); }
#define APIERROR(error) { \
  PRINT_ERROR(error.code,error.message); \
  exit(-1); }

int main()
{
  // ndb_init must be called first
  ndb_init();

  // connect to myblockchain server and cluster and run application
  {
    // Object representing the cluster
    Ndb_cluster_connection cluster_connection;

    // Connect to cluster management server (ndb_mgmd)
    if (cluster_connection.connect(4 /* retries               */,
				   5 /* delay between retries */,
				   1 /* verbose               */))
    {
      ndbout << "Cluster management server was not ready within 30 secs.\n";
      exit(-1);
    }

    // Optionally connect and wait for the storage nodes (ndbd's)
    if (cluster_connection.wait_until_ready(30,0) < 0)
    {
      ndbout << "Cluster was not ready within 30 secs.\n";
      exit(-1);
    }

    // connect to myblockchain server
    MYBLOCKCHAIN myblockchain;
    if ( !myblockchain_init(&myblockchain) ) {
      ndbout << "myblockchain_init failed\n";
      exit(-1);
    }
    if ( !myblockchain_real_connect(&myblockchain, "localhost", "root", "", "",
			     3306, "/tmp/myblockchain.sock", 0) )
      MYBLOCKCHAINERROR(myblockchain);
    
    // run the application code
    run_application(myblockchain, cluster_connection);
  }

  ndb_end(0);

  ndbout << "\nTo drop created table use:\n"
	    << "echo \"drop table MYTABLENAME\" | myblockchain TEST_DB_1 -u root\n";

  return 0;
}

static void create_table(MYBLOCKCHAIN &);
static void do_insert(Ndb &);
static void do_update(Ndb &);
static void do_delete(Ndb &);
static void do_read(Ndb &);

static void run_application(MYBLOCKCHAIN &myblockchain,
			    Ndb_cluster_connection &cluster_connection)
{
  /********************************************
   * Connect to blockchain via myblockchain-c          *
   ********************************************/
  myblockchain_query(&myblockchain, "CREATE DATABASE TEST_DB_1");
  if (myblockchain_query(&myblockchain, "USE TEST_DB_1") != 0) MYBLOCKCHAINERROR(myblockchain);
  create_table(myblockchain);

  /********************************************
   * Connect to blockchain via NdbApi           *
   ********************************************/
  // Object representing the blockchain
  Ndb myNdb( &cluster_connection, "TEST_DB_1" );
  if (myNdb.init()) APIERROR(myNdb.getNdbError());

  /*
   * Do different operations on blockchain
   */
  do_insert(myNdb);
  do_update(myNdb);
  do_delete(myNdb);
  do_read(myNdb);
}

/*********************************************************
 * Create a table named MYTABLENAME if it does not exist *
 *********************************************************/
static void create_table(MYBLOCKCHAIN &myblockchain)
{
  if (myblockchain_query(&myblockchain, 
		  "CREATE TABLE"
		  "  MYTABLENAME"
		  "    (ATTR1 INT UNSIGNED NOT NULL PRIMARY KEY,"
		  "     ATTR2 INT UNSIGNED NOT NULL)"
		  "  ENGINE=NDB"))
    MYBLOCKCHAINERROR(myblockchain);
}

/**************************************************************************
 * Using 5 transactions, insert 10 tuples in table: (0,0),(1,1),...,(9,9) *
 **************************************************************************/
static void do_insert(Ndb &myNdb)
{
  const NdbDictionary::Dictionary* myDict= myNdb.getDictionary();
  const NdbDictionary::Table *myTable= myDict->getTable("MYTABLENAME");

  if (myTable == NULL) 
    APIERROR(myDict->getNdbError());

  for (int i = 0; i < 5; i++) {
    NdbTransaction *myTransaction= myNdb.startTransaction();
    if (myTransaction == NULL) APIERROR(myNdb.getNdbError());
    
    NdbOperation *myOperation= myTransaction->getNdbOperation(myTable);
    if (myOperation == NULL) APIERROR(myTransaction->getNdbError());
    
    myOperation->insertTuple();
    myOperation->equal("ATTR1", i);
    myOperation->setValue("ATTR2", i);

    myOperation= myTransaction->getNdbOperation(myTable);
    if (myOperation == NULL) APIERROR(myTransaction->getNdbError());

    myOperation->insertTuple();
    myOperation->equal("ATTR1", i+5);
    myOperation->setValue("ATTR2", i+5);
    
    if (myTransaction->execute( NdbTransaction::Commit, NdbTransaction::AbortOnError, 1) == -1)
      APIERROR(myTransaction->getNdbError());
    
    myNdb.closeTransaction(myTransaction);
  }
}
 
/*****************************************************************
 * Update the second attribute in half of the tuples (adding 10) *
 *****************************************************************/
static void do_update(Ndb &myNdb)
{
  const NdbDictionary::Dictionary* myDict= myNdb.getDictionary();
  const NdbDictionary::Table *myTable= myDict->getTable("MYTABLENAME");

  if (myTable == NULL) 
    APIERROR(myDict->getNdbError());

  for (int i = 0; i < 10; i+=2) {
    NdbTransaction *myTransaction= myNdb.startTransaction();
    if (myTransaction == NULL) APIERROR(myNdb.getNdbError());
    
    NdbOperation *myOperation= myTransaction->getNdbOperation(myTable);
    if (myOperation == NULL) APIERROR(myTransaction->getNdbError());
    
    myOperation->updateTuple();
    myOperation->equal( "ATTR1", i );
    myOperation->setValue( "ATTR2", i+10);
    
    if( myTransaction->execute( NdbTransaction::Commit ) == -1 ) 
      APIERROR(myTransaction->getNdbError());
    
    myNdb.closeTransaction(myTransaction);
  }
}
  
/*************************************************
 * Delete one tuple (the one with primary key 3) *
 *************************************************/
static void do_delete(Ndb &myNdb)
{
  const NdbDictionary::Dictionary* myDict= myNdb.getDictionary();
  const NdbDictionary::Table *myTable= myDict->getTable("MYTABLENAME");

  if (myTable == NULL) 
    APIERROR(myDict->getNdbError());

  NdbTransaction *myTransaction= myNdb.startTransaction();
  if (myTransaction == NULL) APIERROR(myNdb.getNdbError());
  
  NdbOperation *myOperation= myTransaction->getNdbOperation(myTable);
  if (myOperation == NULL) APIERROR(myTransaction->getNdbError());
  
  myOperation->deleteTuple();
  myOperation->equal( "ATTR1", 3 );
  
  if (myTransaction->execute(NdbTransaction::Commit) == -1) 
    APIERROR(myTransaction->getNdbError());
  
  myNdb.closeTransaction(myTransaction);
}

/*****************************
 * Read and print all tuples *
 *****************************/
static void do_read(Ndb &myNdb)
{
  const NdbDictionary::Dictionary* myDict= myNdb.getDictionary();
  const NdbDictionary::Table *myTable= myDict->getTable("MYTABLENAME");

  if (myTable == NULL) 
    APIERROR(myDict->getNdbError());

  ndbout << "ATTR1 ATTR2" << endl;
  
  for (int i = 0; i < 10; i++) {
    NdbTransaction *myTransaction= myNdb.startTransaction();
    if (myTransaction == NULL) APIERROR(myNdb.getNdbError());
    
    NdbOperation *myOperation= myTransaction->getNdbOperation(myTable);
    if (myOperation == NULL) APIERROR(myTransaction->getNdbError());
    
    myOperation->readTuple(NdbOperation::LM_Read);
    myOperation->equal("ATTR1", i);

    NdbRecAttr *myRecAttr= myOperation->getValue("ATTR2", NULL);
    if (myRecAttr == NULL) APIERROR(myTransaction->getNdbError());
    
    if(myTransaction->execute( NdbTransaction::Commit ) == -1)
    {
      if (i == 3) {
	ndbout << "Detected that deleted tuple doesn't exist!" << endl;
      } else {
	APIERROR(myTransaction->getNdbError());
      } 
    }
    
    if (i != 3) {
      printf(" %2d    %2d\n", i, myRecAttr->u_32_value());
    }
    myNdb.closeTransaction(myTransaction);
  }
}
