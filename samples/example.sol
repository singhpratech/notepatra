// SPDX-License-Identifier: MIT
// Notepatra palette preview - synthetic; no real data
// Exercises: pragma, contract, function visibility, modifiers, mapping,
// events, require, msg.sender, address, uint256, control flow.

pragma solidity ^0.8.20;

contract SimpleVault {
    address public immutable owner;
    uint256 public totalDeposits;
    bool private locked;

    mapping(address => uint256) public balances;
    mapping(address => bool) private admins;

    event Deposited(address indexed who, uint256 amount);
    event Withdrawn(address indexed who, uint256 amount);
    event AdminChanged(address indexed who, bool isAdmin);

    error NotOwner(address caller);
    error InsufficientBalance(uint256 requested, uint256 available);

    modifier onlyOwner() {
        if (msg.sender != owner) revert NotOwner(msg.sender);
        _;
    }

    modifier nonReentrant() {
        require(!locked, "reentrant call");
        locked = true;
        _;
        locked = false;
    }

    constructor() {
        owner = msg.sender;
        admins[msg.sender] = true;
    }

    function setAdmin(address who, bool flag) external onlyOwner {
        admins[who] = flag;
        emit AdminChanged(who, flag);
    }

    function deposit() external payable {
        require(msg.value > 0, "zero deposit");
        balances[msg.sender] += msg.value;
        totalDeposits += msg.value;
        emit Deposited(msg.sender, msg.value);
    }

    function withdraw(uint256 amount) external nonReentrant {
        uint256 bal = balances[msg.sender];
        if (amount > bal) revert InsufficientBalance(amount, bal);

        balances[msg.sender] = bal - amount;
        totalDeposits -= amount;

        (bool ok, ) = msg.sender.call{value: amount}("");
        require(ok, "transfer failed");
        emit Withdrawn(msg.sender, amount);
    }

    function isAdmin(address who) external view returns (bool) {
        return admins[who];
    }

    receive() external payable {
        balances[msg.sender] += msg.value;
        totalDeposits += msg.value;
        emit Deposited(msg.sender, msg.value);
    }
}
