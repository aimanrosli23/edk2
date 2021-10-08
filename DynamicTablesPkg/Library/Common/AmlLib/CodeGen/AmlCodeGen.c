/** @file
  AML Code Generation.

  Copyright (c) 2020 - 2021, Arm Limited. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <AmlNodeDefines.h>

#include <AcpiTableGenerator.h>

#include <AmlCoreInterface.h>
#include <AmlEncoding/Aml.h>
#include <CodeGen/AmlResourceDataCodeGen.h>
#include <Tree/AmlNode.h>
#include <Tree/AmlTree.h>
#include <String/AmlString.h>
#include <Utils/AmlUtility.h>

/** Utility function to link a node when returning from a CodeGen function.

  @param [in]  Node           Newly created node.
  @param [in]  ParentNode     If provided, set ParentNode as the parent
                              of the node created.
  @param [out] NewObjectNode  If not NULL:
                               - and Success, contains the created Node.
                               - and Error, reset to NULL.

  @retval  EFI_SUCCESS            The function completed successfully.
  @retval  EFI_INVALID_PARAMETER  Invalid parameter.
**/
STATIC
EFI_STATUS
EFIAPI
LinkNode (
  IN  AML_OBJECT_NODE    * Node,
  IN  AML_NODE_HEADER    * ParentNode,
  OUT AML_OBJECT_NODE   ** NewObjectNode
  )
{
  EFI_STATUS    Status;

  if (NewObjectNode != NULL) {
    *NewObjectNode = NULL;
  }

  // Add RdNode as the last element.
  if (ParentNode != NULL) {
    Status = AmlVarListAddTail (ParentNode, (AML_NODE_HEADER*)Node);
    if (EFI_ERROR (Status)) {
      ASSERT (0);
      return Status;
    }
  }

  if (NewObjectNode != NULL) {
    *NewObjectNode = Node;
  }

  return EFI_SUCCESS;
}

/** AML code generation for DefinitionBlock.

  Create a Root Node handle.
  It is the caller's responsibility to free the allocated memory
  with the AmlDeleteTree function.

  AmlCodeGenDefinitionBlock (TableSignature, OemID, TableID, OEMRevision) is
  equivalent to the following ASL code:
    DefinitionBlock (AMLFileName, TableSignature, ComplianceRevision,
      OemID, TableID, OEMRevision) {}
  with the ComplianceRevision set to 2 and the AMLFileName is ignored.

  @param[in]  TableSignature       4-character ACPI signature.
                                   Must be 'DSDT' or 'SSDT'.
  @param[in]  OemId                6-character string OEM identifier.
  @param[in]  OemTableId           8-character string OEM table identifier.
  @param[in]  OemRevision          OEM revision number.
  @param[out] NewRootNode          Pointer to the root node representing a
                                   Definition Block.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenDefinitionBlock (
  IN  CONST CHAR8             * TableSignature,
  IN  CONST CHAR8             * OemId,
  IN  CONST CHAR8             * OemTableId,
  IN        UINT32              OemRevision,
  OUT       AML_ROOT_NODE    ** NewRootNode
  )
{
  EFI_STATUS                      Status;
  EFI_ACPI_DESCRIPTION_HEADER     AcpiHeader;

  if ((TableSignature == NULL)  ||
      (OemId == NULL)           ||
      (OemTableId == NULL)      ||
      (NewRootNode == NULL)) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (&AcpiHeader.Signature, TableSignature, 4);
  AcpiHeader.Length = sizeof (EFI_ACPI_DESCRIPTION_HEADER);
  AcpiHeader.Revision = 2;
  CopyMem (&AcpiHeader.OemId, OemId, 6);
  CopyMem (&AcpiHeader.OemTableId, OemTableId, 8);
  AcpiHeader.OemRevision = OemRevision;
  AcpiHeader.CreatorId = TABLE_GENERATOR_CREATOR_ID_ARM;
  AcpiHeader.CreatorRevision = CREATE_REVISION (1, 0);

  Status = AmlCreateRootNode (&AcpiHeader, NewRootNode);
  ASSERT_EFI_ERROR (Status);

  return Status;
}

/** AML code generation for a String object node.

  @param [in]  String          Pointer to a NULL terminated string.
  @param [out] NewObjectNode   If success, contains the created
                               String object node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenString (
  IN  CHAR8               * String,
  OUT AML_OBJECT_NODE    ** NewObjectNode
  )
{
  EFI_STATUS          Status;
  AML_OBJECT_NODE   * ObjectNode;
  AML_DATA_NODE     * DataNode;

  if ((String == NULL)  ||
      (NewObjectNode == NULL)) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  DataNode = NULL;

  Status = AmlCreateObjectNode (
             AmlGetByteEncodingByOpCode (AML_STRING_PREFIX, 0),
             0,
             &ObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeString,
             (UINT8*)String,
             (UINT32)AsciiStrLen (String) + 1,
             &DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  Status = AmlSetFixedArgument (
             ObjectNode,
             EAmlParseIndexTerm0,
             (AML_NODE_HEADER*)DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    AmlDeleteTree ((AML_NODE_HEADER*)DataNode);
    goto error_handler;
  }

  *NewObjectNode = ObjectNode;
  return Status;

error_handler:
  AmlDeleteTree ((AML_NODE_HEADER*)ObjectNode);
  return Status;
}

/** AML code generation for an Integer object node.

  @param [in]  Integer         Integer of the Integer object node.
  @param [out] NewObjectNode   If success, contains the created
                               Integer object node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenInteger (
  IN  UINT64                Integer,
  OUT AML_OBJECT_NODE    ** NewObjectNode
  )
{
  EFI_STATUS          Status;
  INT8                ValueWidthDiff;

  if (NewObjectNode == NULL) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

   // Create an object node containing Zero.
   Status = AmlCreateObjectNode (
             AmlGetByteEncodingByOpCode (AML_ZERO_OP, 0),
             0,
             NewObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  // Update the object node with integer value.
  Status = AmlNodeSetIntegerValue (*NewObjectNode, Integer, &ValueWidthDiff);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    AmlDeleteTree ((AML_NODE_HEADER*)*NewObjectNode);
  }

  return Status;
}

/** AML code generation for a Package object node.

  The package generated is empty. New elements can be added via its
  list of variable arguments.

  @param [out] NewObjectNode   If success, contains the created
                               Package object node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenPackage (
  OUT AML_OBJECT_NODE    ** NewObjectNode
  )
{
  EFI_STATUS        Status;
  AML_DATA_NODE   * DataNode;
  UINT8             NodeCount;

  if (NewObjectNode == NULL) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  NodeCount = 0;

  // Create an object node.
  // PkgLen is 2:
  //  - one byte to store the PkgLength
  //  - one byte for the NumElements.
  // Cf ACPI6.3, s20.2.5 "Term Objects Encoding"
  // DefPackage  := PackageOp PkgLength NumElements PackageElementList
  // NumElements := ByteData
  Status = AmlCreateObjectNode (
             AmlGetByteEncodingByOpCode (AML_PACKAGE_OP, 0),
             2,
             NewObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  // NumElements is a ByteData.
  Status = AmlCreateDataNode (
             EAmlNodeDataTypeUInt,
             &NodeCount,
             sizeof (NodeCount),
             &DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  Status = AmlSetFixedArgument (
             *NewObjectNode,
             EAmlParseIndexTerm0,
             (AML_NODE_HEADER*)DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  return Status;

error_handler:
  AmlDeleteTree ((AML_NODE_HEADER*)*NewObjectNode);
  if (DataNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)DataNode);
  }
  return Status;
}

/** AML code generation for a Buffer object node.

  To create a Buffer object node with an empty buffer,
  call the function with (Buffer=NULL, BufferSize=0).

  @param [in]  Buffer          Buffer to set for the created Buffer
                               object node. The Buffer's content is copied.
                               NULL if there is no buffer to set for
                               the Buffer node.
  @param [in]  BufferSize      Size of the Buffer.
                               0 if there is no buffer to set for
                               the Buffer node.
  @param [out] NewObjectNode   If success, contains the created
                               Buffer object node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenBuffer (
  IN  CONST UINT8             * Buffer,       OPTIONAL
  IN        UINT32              BufferSize,   OPTIONAL
  OUT       AML_OBJECT_NODE  ** NewObjectNode
  )
{
  EFI_STATUS        Status;
  AML_OBJECT_NODE * BufferNode;
  AML_OBJECT_NODE * BufferSizeNode;
  UINT32            BufferSizeNodeSize;
  AML_DATA_NODE   * DataNode;
  UINT32            PkgLen;

  // Buffer and BufferSize must be either both set, or both clear.
  if ((NewObjectNode == NULL)                 ||
      ((Buffer == NULL) != (BufferSize == 0))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  BufferNode = NULL;
  DataNode = NULL;

  // Cf ACPI 6.3 specification, s20.2.5.4 "Type 2 Opcodes Encoding"
  // DefBuffer := BufferOp PkgLength BufferSize ByteList
  // BufferOp  := 0x11
  // BufferSize := TermArg => Integer

  Status = AmlCodeGenInteger (BufferSize, &BufferSizeNode);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  // Get the number of bytes required to encode the BufferSizeNode.
  Status = AmlComputeSize (
             (AML_NODE_HEADER*)BufferSizeNode,
             &BufferSizeNodeSize
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  // Compute the size to write in the PkgLen.
  Status = AmlComputePkgLength (BufferSizeNodeSize + BufferSize, &PkgLen);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  // Create an object node for the buffer.
  Status = AmlCreateObjectNode (
             AmlGetByteEncodingByOpCode (AML_BUFFER_OP, 0),
             PkgLen,
             &BufferNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  // Set the BufferSizeNode as a fixed argument of the BufferNode.
  Status = AmlSetFixedArgument (
             BufferNode,
             EAmlParseIndexTerm0,
             (AML_NODE_HEADER*)BufferSizeNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  // BufferSizeNode is now attached.
  BufferSizeNode = NULL;

  // If there is a buffer, create a DataNode and attach it to the BufferNode.
  if (Buffer != NULL) {
    Status = AmlCreateDataNode (
               EAmlNodeDataTypeRaw,
               Buffer,
               BufferSize,
               &DataNode
               );
    if (EFI_ERROR (Status)) {
      ASSERT (0);
      goto error_handler;
    }

    Status = AmlVarListAddTail (
               (AML_NODE_HEADER*)BufferNode,
               (AML_NODE_HEADER*)DataNode
               );
    if (EFI_ERROR (Status)) {
      ASSERT (0);
      goto error_handler;
    }
  }

  *NewObjectNode = BufferNode;
  return Status;

error_handler:
  if (BufferSizeNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)BufferSizeNode);
  }
  if (BufferNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)BufferNode);
  }
  if (DataNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)DataNode);
  }
  return Status;
}

/** AML code generation for a ResourceTemplate.

  "ResourceTemplate" is a macro defined in ACPI 6.3, s19.3.3
  "ASL Resource Templates". It allows to store resource data elements.

  In AML, a ResourceTemplate is implemented as a Buffer storing resource
  data elements. An EndTag resource data descriptor must be at the end
  of the list of resource data elements.
  This function generates a Buffer node with an EndTag resource data
  descriptor. It can be seen as an empty list of resource data elements.

  @param [out] NewObjectNode   If success, contains the created
                               ResourceTemplate object node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenResourceTemplate (
  OUT AML_OBJECT_NODE    ** NewObjectNode
  )
{
  EFI_STATUS          Status;
  AML_OBJECT_NODE   * BufferNode;

  if (NewObjectNode == NULL) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  // Create a BufferNode with an empty buffer.
  Status = AmlCodeGenBuffer (NULL, 0, &BufferNode);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  // Create an EndTag resource data element and attach it to the Buffer.
  Status = AmlCodeGenEndTag (0, BufferNode, NULL);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    AmlDeleteTree ((AML_NODE_HEADER*)BufferNode);
    return Status;
  }

  *NewObjectNode = BufferNode;
  return Status;
}

/** AML code generation for a Name object node.

  @param  [in] NameString     The new variable name.
                              Must be a NULL-terminated ASL NameString
                              e.g.: "DEV0", "DV15.DEV0", etc.
                              This input string is copied.
  @param [in]  Object         Object associated to the NameString.
  @param [in]  ParentNode     If provided, set ParentNode as the parent
                              of the node created.
  @param [out] NewObjectNode  If success, contains the created node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenName (
  IN  CONST CHAR8              * NameString,
  IN        AML_OBJECT_NODE    * Object,
  IN        AML_NODE_HEADER    * ParentNode,     OPTIONAL
  OUT       AML_OBJECT_NODE   ** NewObjectNode   OPTIONAL
  )
{
  EFI_STATUS          Status;
  AML_OBJECT_NODE   * ObjectNode;
  AML_DATA_NODE     * DataNode;
  CHAR8             * AmlNameString;
  UINT32              AmlNameStringSize;

  if ((NameString == NULL)    ||
      (Object == NULL)        ||
      ((ParentNode == NULL) && (NewObjectNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  ObjectNode = NULL;
  DataNode = NULL;
  AmlNameString = NULL;

  Status = ConvertAslNameToAmlName (NameString, &AmlNameString);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  Status = AmlGetNameStringSize (AmlNameString, &AmlNameStringSize);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler1;
  }

  Status = AmlCreateObjectNode (
             AmlGetByteEncodingByOpCode (AML_NAME_OP, 0),
             0,
             &ObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler1;
  }

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeNameString,
             (UINT8*)AmlNameString,
             AmlNameStringSize,
             &DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  Status = AmlSetFixedArgument (
             ObjectNode,
             EAmlParseIndexTerm0,
             (AML_NODE_HEADER*)DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    AmlDeleteTree ((AML_NODE_HEADER*)DataNode);
    goto error_handler2;
  }

  Status = AmlSetFixedArgument (
             ObjectNode,
             EAmlParseIndexTerm1,
             (AML_NODE_HEADER*)Object
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  Status = LinkNode (
             ObjectNode,
             ParentNode,
             NewObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  // Free AmlNameString before returning as it is copied
  // in the call to AmlCreateDataNode().
  goto error_handler1;

error_handler2:
  if (ObjectNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)ObjectNode);
  }

error_handler1:
  if (AmlNameString != NULL) {
    FreePool (AmlNameString);
  }

  return Status;
}

/** AML code generation for a Name object node, containing a String.

  AmlCodeGenNameString ("_HID", "HID0000", ParentNode, NewObjectNode) is
  equivalent of the following ASL code:
    Name(_HID, "HID0000")

  @param  [in] NameString     The new variable name.
                              Must be a NULL-terminated ASL NameString
                              e.g.: "DEV0", "DV15.DEV0", etc.
                              The input string is copied.
  @param [in]  String         NULL terminated String to associate to the
                              NameString.
  @param [in]  ParentNode     If provided, set ParentNode as the parent
                              of the node created.
  @param [out] NewObjectNode  If success, contains the created node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenNameString (
  IN  CONST CHAR8              * NameString,
  IN        CHAR8              * String,
  IN        AML_NODE_HEADER    * ParentNode,     OPTIONAL
  OUT       AML_OBJECT_NODE   ** NewObjectNode   OPTIONAL
  )
{
  EFI_STATUS          Status;
  AML_OBJECT_NODE   * ObjectNode;

  if ((NameString == NULL)  ||
      (String == NULL)      ||
      ((ParentNode == NULL) && (NewObjectNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  Status = AmlCodeGenString (String, &ObjectNode);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  Status = AmlCodeGenName (
             NameString,
             ObjectNode,
             ParentNode,
             NewObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    AmlDeleteTree ((AML_NODE_HEADER*)ObjectNode);
  }

  return Status;
}

/** AML code generation for a Name object node, containing an Integer.

  AmlCodeGenNameInteger ("_UID", 1, ParentNode, NewObjectNode) is
  equivalent of the following ASL code:
    Name(_UID, One)

  @param  [in] NameString     The new variable name.
                              Must be a NULL-terminated ASL NameString
                              e.g.: "DEV0", "DV15.DEV0", etc.
                              The input string is copied.
  @param [in]  Integer        Integer to associate to the NameString.
  @param [in]  ParentNode     If provided, set ParentNode as the parent
                              of the node created.
  @param [out] NewObjectNode  If success, contains the created node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenNameInteger (
  IN  CONST CHAR8              * NameString,
  IN        UINT64               Integer,
  IN        AML_NODE_HEADER    * ParentNode,     OPTIONAL
  OUT       AML_OBJECT_NODE   ** NewObjectNode   OPTIONAL
  )
{
  EFI_STATUS          Status;
  AML_OBJECT_NODE   * ObjectNode;

  if ((NameString == NULL)  ||
      ((ParentNode == NULL) && (NewObjectNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  Status = AmlCodeGenInteger (Integer, &ObjectNode);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  Status = AmlCodeGenName (
             NameString,
             ObjectNode,
             ParentNode,
             NewObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    AmlDeleteTree ((AML_NODE_HEADER*)ObjectNode);
  }

  return Status;
}

/** AML code generation for a Device object node.

  AmlCodeGenDevice ("COM0", ParentNode, NewObjectNode) is
  equivalent of the following ASL code:
    Device(COM0) {}

  @param  [in] NameString     The new Device's name.
                              Must be a NULL-terminated ASL NameString
                              e.g.: "DEV0", "DV15.DEV0", etc.
                              The input string is copied.
  @param [in]  ParentNode     If provided, set ParentNode as the parent
                              of the node created.
  @param [out] NewObjectNode  If success, contains the created node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenDevice (
  IN  CONST CHAR8              * NameString,
  IN        AML_NODE_HEADER    * ParentNode,     OPTIONAL
  OUT       AML_OBJECT_NODE   ** NewObjectNode   OPTIONAL
  )
{
  EFI_STATUS          Status;
  AML_OBJECT_NODE   * ObjectNode;
  AML_DATA_NODE     * DataNode;
  CHAR8             * AmlNameString;
  UINT32              AmlNameStringSize;

  if ((NameString == NULL)  ||
      ((ParentNode == NULL) && (NewObjectNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  ObjectNode = NULL;
  DataNode = NULL;
  AmlNameString = NULL;

  Status = ConvertAslNameToAmlName (NameString, &AmlNameString);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  Status = AmlGetNameStringSize (AmlNameString, &AmlNameStringSize);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler1;
  }

  Status = AmlCreateObjectNode (
             AmlGetByteEncodingByOpCode (AML_EXT_OP, AML_EXT_DEVICE_OP),
             AmlNameStringSize + AmlComputePkgLengthWidth (AmlNameStringSize),
             &ObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler1;
  }

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeNameString,
             (UINT8*)AmlNameString,
             AmlNameStringSize,
             &DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  Status = AmlSetFixedArgument (
             ObjectNode,
             EAmlParseIndexTerm0,
             (AML_NODE_HEADER*)DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    AmlDeleteTree ((AML_NODE_HEADER*)DataNode);
    goto error_handler2;
  }

  Status = LinkNode (
             ObjectNode,
             ParentNode,
             NewObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  // Free AmlNameString before returning as it is copied
  // in the call to AmlCreateDataNode().
  goto error_handler1;

error_handler2:
  if (ObjectNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)ObjectNode);
  }

error_handler1:
  if (AmlNameString != NULL) {
    FreePool (AmlNameString);
  }

  return Status;
}

/** AML code generation for a Scope object node.

  AmlCodeGenScope ("_SB", ParentNode, NewObjectNode) is
  equivalent of the following ASL code:
    Scope(_SB) {}

  @param  [in] NameString     The new Scope's name.
                              Must be a NULL-terminated ASL NameString
                              e.g.: "DEV0", "DV15.DEV0", etc.
                              The input string is copied.
  @param [in]  ParentNode     If provided, set ParentNode as the parent
                              of the node created.
  @param [out] NewObjectNode  If success, contains the created node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenScope (
  IN  CONST CHAR8              * NameString,
  IN        AML_NODE_HEADER    * ParentNode,     OPTIONAL
  OUT       AML_OBJECT_NODE   ** NewObjectNode   OPTIONAL
  )
{
  EFI_STATUS          Status;
  AML_OBJECT_NODE   * ObjectNode;
  AML_DATA_NODE     * DataNode;
  CHAR8             * AmlNameString;
  UINT32              AmlNameStringSize;

  if ((NameString == NULL)  ||
      ((ParentNode == NULL) && (NewObjectNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  ObjectNode = NULL;
  DataNode = NULL;
  AmlNameString = NULL;

  Status = ConvertAslNameToAmlName (NameString, &AmlNameString);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  Status = AmlGetNameStringSize (AmlNameString, &AmlNameStringSize);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler1;
  }

  Status = AmlCreateObjectNode (
             AmlGetByteEncodingByOpCode (AML_SCOPE_OP, 0),
             AmlNameStringSize + AmlComputePkgLengthWidth (AmlNameStringSize),
             &ObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler1;
  }

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeNameString,
             (UINT8*)AmlNameString,
             AmlNameStringSize,
             &DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  Status = AmlSetFixedArgument (
             ObjectNode,
             EAmlParseIndexTerm0,
             (AML_NODE_HEADER*)DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    AmlDeleteTree ((AML_NODE_HEADER*)DataNode);
    goto error_handler2;
  }

  Status = LinkNode (
             ObjectNode,
             ParentNode,
             NewObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  // Free AmlNameString before returning as it is copied
  // in the call to AmlCreateDataNode().
  goto error_handler1;

error_handler2:
  if (ObjectNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)ObjectNode);
  }

error_handler1:
  if (AmlNameString != NULL) {
    FreePool (AmlNameString);
  }

  return Status;
}

/** AML code generation for a Method object node.

  AmlCodeGenMethod ("MET0", 1, TRUE, 3, ParentNode, NewObjectNode) is
  equivalent of the following ASL code:
    Method(MET0, 1, Serialized, 3) {}

  ACPI 6.4, s20.2.5.2 "Named Objects Encoding":
    DefMethod := MethodOp PkgLength NameString MethodFlags TermList
    MethodOp := 0x14

  The ASL parameters "ReturnType" and "ParameterTypes" are not asked
  in this function. They are optional parameters in ASL.

  @param [in]  NameString     The new Method's name.
                              Must be a NULL-terminated ASL NameString
                              e.g.: "MET0", "_SB.MET0", etc.
                              The input string is copied.
  @param [in]  NumArgs        Number of arguments.
                              Must be 0 <= NumArgs <= 6.
  @param [in]  IsSerialized   TRUE is equivalent to Serialized.
                              FALSE is equivalent to NotSerialized.
                              Default is NotSerialized in ASL spec.
  @param [in]  SyncLevel      Synchronization level for the method.
                              Must be 0 <= SyncLevel <= 15.
                              Default is 0 in ASL.
  @param [in]  ParentNode     If provided, set ParentNode as the parent
                              of the node created.
  @param [out] NewObjectNode  If success, contains the created node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenMethod (
  IN  CONST CHAR8              * NameString,
  IN        UINT8                NumArgs,
  IN        BOOLEAN              IsSerialized,
  IN        UINT8                SyncLevel,
  IN        AML_NODE_HEADER    * ParentNode,     OPTIONAL
  OUT       AML_OBJECT_NODE   ** NewObjectNode   OPTIONAL
  )
{
  EFI_STATUS        Status;
  UINT32            PkgLen;
  UINT8             Flags;
  AML_OBJECT_NODE * ObjectNode;
  AML_DATA_NODE   * DataNode;
  CHAR8           * AmlNameString;
  UINT32            AmlNameStringSize;

  if ((NameString == NULL)    ||
      (NumArgs > 6)           ||
      (SyncLevel > 15)        ||
      ((ParentNode == NULL) && (NewObjectNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  ObjectNode = NULL;
  DataNode = NULL;

  // ACPI 6.4, s20.2.5.2 "Named Objects Encoding":
  //   DefMethod := MethodOp PkgLength NameString MethodFlags TermList
  //   MethodOp := 0x14
  // So:
  //  1- Create the NameString
  //  2- Compute the size to write in the PkgLen
  //  3- Create nodes for the NameString and Method object node
  //  4- Set the NameString DataNode as a fixed argument
  //  5- Create and link the MethodFlags node

  // 1- Create the NameString
  Status = ConvertAslNameToAmlName (NameString, &AmlNameString);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  Status = AmlGetNameStringSize (AmlNameString, &AmlNameStringSize);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler1;
  }

  // 2- Compute the size to write in the PkgLen
  //    Add 1 byte (ByteData) for MethodFlags.
  Status = AmlComputePkgLength (AmlNameStringSize + 1, &PkgLen);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler1;
  }

  //  3- Create nodes for the NameString and Method object node
  Status = AmlCreateObjectNode (
             AmlGetByteEncodingByOpCode (AML_METHOD_OP, 0),
             PkgLen,
             &ObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler1;
  }

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeNameString,
             (UINT8*)AmlNameString,
             AmlNameStringSize,
             &DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  //  4- Set the NameString DataNode as a fixed argument
  Status = AmlSetFixedArgument (
             ObjectNode,
             EAmlParseIndexTerm0,
             (AML_NODE_HEADER*)DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  DataNode = NULL;

  //  5- Create and link the MethodFlags node
  Flags = NumArgs                   |
          (IsSerialized ? BIT3 : 0) |
          (SyncLevel << 4);

  Status = AmlCreateDataNode (EAmlNodeDataTypeUInt, &Flags, 1, &DataNode);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  Status = AmlSetFixedArgument (
             ObjectNode,
             EAmlParseIndexTerm1,
             (AML_NODE_HEADER*)DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  // Data node is attached so set the pointer to
  // NULL to ensure correct error handling.
  DataNode = NULL;

  Status = LinkNode (
             ObjectNode,
             ParentNode,
             NewObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler2;
  }

  // Free AmlNameString before returning as it is copied
  // in the call to AmlCreateDataNode().
  goto error_handler1;

error_handler2:
  if (ObjectNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)ObjectNode);
  }
  if (DataNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)DataNode);
  }

error_handler1:
  if (AmlNameString != NULL) {
    FreePool (AmlNameString);
  }
  return Status;
}

/** AML code generation for a Return object node.

  AmlCodeGenReturn (ReturnNode, ParentNode, NewObjectNode) is
  equivalent of the following ASL code:
    Return([Content of the ReturnNode])

  The ACPI 6.3 specification, s20.2.5.3 "Type 1 Opcodes Encoding" states:
    DefReturn := ReturnOp ArgObject
    ReturnOp := 0xA4
    ArgObject := TermArg => DataRefObject

  Thus, the ReturnNode must be evaluated as a DataRefObject. It can
  be a NameString referencing an object. As this CodeGen Api doesn't
  do semantic checking, it is strongly advised to check the AML bytecode
  generated by this function against an ASL compiler.

  The ReturnNode must be generated inside a Method body scope.

  @param [in]  ReturnNode     The object returned by the Return ASL statement.
                              This node is deleted if an error occurs.
  @param [in]  ParentNode     If provided, set ParentNode as the parent
                              of the node created.
                              Must be a MethodOp node.
  @param [out] NewObjectNode  If success, contains the created node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenReturn (
  IN  AML_NODE_HEADER     * ReturnNode,
  IN  AML_NODE_HEADER     * ParentNode,     OPTIONAL
  OUT AML_OBJECT_NODE    ** NewObjectNode   OPTIONAL
  )
{
  EFI_STATUS        Status;
  AML_OBJECT_NODE * ObjectNode;

  if ((ReturnNode == NULL)                              ||
      ((ParentNode == NULL) && (NewObjectNode == NULL)) ||
      ((ParentNode != NULL) &&
        !AmlNodeCompareOpCode (
            (AML_OBJECT_NODE*)ParentNode, AML_METHOD_OP, 0))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  Status = AmlCreateObjectNode (
             AmlGetByteEncodingByOpCode (AML_RETURN_OP, 0),
             0,
             &ObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  Status = AmlSetFixedArgument (
             ObjectNode,
             EAmlParseIndexTerm0,
             (AML_NODE_HEADER*)ReturnNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  ReturnNode = NULL;

  Status = LinkNode (
             ObjectNode,
             ParentNode,
             NewObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  return Status;

error_handler:
  if (ReturnNode != NULL) {
    AmlDeleteTree (ReturnNode);
  }
  if (ObjectNode != NULL) {
    AmlDeleteTree ((AML_NODE_HEADER*)ObjectNode);
  }
  return Status;
}

/** AML code generation for a Return object node,
    returning the object as an input NameString.

  AmlCodeGenReturn ("NAM1", ParentNode, NewObjectNode) is
  equivalent of the following ASL code:
    Return(NAM1)

  The ACPI 6.3 specification, s20.2.5.3 "Type 1 Opcodes Encoding" states:
    DefReturn := ReturnOp ArgObject
    ReturnOp := 0xA4
    ArgObject := TermArg => DataRefObject

  Thus, the ReturnNode must be evaluated as a DataRefObject. It can
  be a NameString referencing an object. As this CodeGen Api doesn't
  do semantic checking, it is strongly advised to check the AML bytecode
  generated by this function against an ASL compiler.

  The ReturnNode must be generated inside a Method body scope.

  @param [in]  NameString     The object referenced by this NameString
                              is returned by the Return ASL statement.
                              Must be a NULL-terminated ASL NameString
                              e.g.: "NAM1", "_SB.NAM1", etc.
                              The input string is copied.
  @param [in]  ParentNode     If provided, set ParentNode as the parent
                              of the node created.
                              Must be a MethodOp node.
  @param [out] NewObjectNode  If success, contains the created node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
STATIC
EFI_STATUS
EFIAPI
AmlCodeGenReturnNameString (
  IN  CONST CHAR8               * NameString,
  IN        AML_NODE_HEADER     * ParentNode,     OPTIONAL
  OUT       AML_OBJECT_NODE    ** NewObjectNode   OPTIONAL
  )
{
  EFI_STATUS          Status;
  AML_DATA_NODE     * DataNode;
  CHAR8             * AmlNameString;
  UINT32              AmlNameStringSize;

  DataNode = NULL;

  Status = ConvertAslNameToAmlName (NameString, &AmlNameString);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  Status = AmlGetNameStringSize (AmlNameString, &AmlNameStringSize);
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto exit_handler;
  }

  Status = AmlCreateDataNode (
             EAmlNodeDataTypeNameString,
             (UINT8*)AmlNameString,
             AmlNameStringSize,
             &DataNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto exit_handler;
  }

  // AmlCodeGenReturn() deletes DataNode if error.
  Status = AmlCodeGenReturn (
             (AML_NODE_HEADER*)DataNode,
             ParentNode,
             NewObjectNode
             );
  ASSERT_EFI_ERROR (Status);

exit_handler:
  if (AmlNameString != NULL) {
    FreePool (AmlNameString);
  }
  return Status;
}

/** AML code generation for a method returning a NameString.

  AmlCodeGenMethodRetNameString (
    "MET0", "_CRS", 1, TRUE, 3, ParentNode, NewObjectNode
    );
  is equivalent of the following ASL code:
    Method(MET0, 1, Serialized, 3) {
      Return (_CRS)
    }

  The ASL parameters "ReturnType" and "ParameterTypes" are not asked
  in this function. They are optional parameters in ASL.

  @param [in]  MethodNameString     The new Method's name.
                                    Must be a NULL-terminated ASL NameString
                                    e.g.: "MET0", "_SB.MET0", etc.
                                    The input string is copied.
  @param [in]  ReturnedNameString   The name of the object returned by the
                                    method. Optional parameter, can be:
                                     - NULL (ignored).
                                     - A NULL-terminated ASL NameString.
                                       e.g.: "MET0", "_SB.MET0", etc.
                                       The input string is copied.
  @param [in]  NumArgs              Number of arguments.
                                    Must be 0 <= NumArgs <= 6.
  @param [in]  IsSerialized         TRUE is equivalent to Serialized.
                                    FALSE is equivalent to NotSerialized.
                                    Default is NotSerialized in ASL spec.
  @param [in]  SyncLevel            Synchronization level for the method.
                                    Must be 0 <= SyncLevel <= 15.
                                    Default is 0 in ASL.
  @param [in]  ParentNode           If provided, set ParentNode as the parent
                                    of the node created.
  @param [out] NewObjectNode        If success, contains the created node.

  @retval EFI_SUCCESS             Success.
  @retval EFI_INVALID_PARAMETER   Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES    Failed to allocate memory.
**/
EFI_STATUS
EFIAPI
AmlCodeGenMethodRetNameString (
  IN  CONST CHAR8                   * MethodNameString,
  IN  CONST CHAR8                   * ReturnedNameString,  OPTIONAL
  IN        UINT8                     NumArgs,
  IN        BOOLEAN                   IsSerialized,
  IN        UINT8                     SyncLevel,
  IN        AML_NODE_HANDLE           ParentNode,          OPTIONAL
  OUT       AML_OBJECT_NODE_HANDLE  * NewObjectNode        OPTIONAL
  )
{
  EFI_STATUS                Status;
  AML_OBJECT_NODE_HANDLE    MethodNode;

  if ((MethodNameString == NULL)  ||
      ((ParentNode == NULL) && (NewObjectNode == NULL))) {
    ASSERT (0);
    return EFI_INVALID_PARAMETER;
  }

  // Create a Method named MethodNameString.
  Status = AmlCodeGenMethod (
             MethodNameString,
             NumArgs,
             IsSerialized,
             SyncLevel,
             NULL,
             &MethodNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    return Status;
  }

  // Return ReturnedNameString if provided.
  if (ReturnedNameString != NULL) {
    Status = AmlCodeGenReturnNameString (
               ReturnedNameString,
               (AML_NODE_HANDLE)MethodNode,
               NULL
               );
    if (EFI_ERROR (Status)) {
      ASSERT (0);
      goto error_handler;
    }
  }

  Status = LinkNode (
             MethodNode,
             ParentNode,
             NewObjectNode
             );
  if (EFI_ERROR (Status)) {
    ASSERT (0);
    goto error_handler;
  }

  return Status;

error_handler:
  if (MethodNode != NULL) {
    AmlDeleteTree ((AML_NODE_HANDLE)MethodNode);
  }
  return Status;
}
