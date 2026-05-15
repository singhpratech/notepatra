// Notepatra palette preview — synthetic; no real data

terraform {
  required_version = ">= 1.5.0"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }

  backend "s3" {
    bucket = "example-tfstate"
    key    = "demo/terraform.tfstate"
    region = "us-east-1"
  }
}

provider "aws" {
  region  = var.region
  profile = "default"
}

variable "region" {
  type    = string
  default = "us-east-1"
}

variable "instance_count" {
  type        = number
  default     = 2
  description = "How many demo instances to launch."
}

variable "tags" {
  type = map(string)
  default = {
    Project = "notepatra-demo"
    Owner   = "alice@example.com"
  }
}

locals {
  name_prefix = "notepatra-${terraform.workspace}"
  common_tags = merge(var.tags, { ManagedBy = "terraform" })
}

data "aws_ami" "ubuntu" {
  most_recent = true
  owners      = ["099720109477"]

  filter {
    name   = "name"
    values = ["ubuntu/images/hvm-ssd/ubuntu-jammy-22.04-amd64-server-*"]
  }
}

resource "aws_instance" "web" {
  count         = var.instance_count
  ami           = data.aws_ami.ubuntu.id
  instance_type = "t3.micro"
  tags          = merge(local.common_tags, { Name = "${local.name_prefix}-${count.index}" })

  dynamic "ebs_block_device" {
    for_each = toset(["xvdb", "xvdc"])
    content {
      device_name = ebs_block_device.value
      volume_size = 10
    }
  }
}

output "instance_ids" {
  value = aws_instance.web[*].id
}

module "vpc" {
  source = "terraform-aws-modules/vpc/aws"
  name   = "${local.name_prefix}-vpc"
  cidr   = "10.0.0.0/16"
}
